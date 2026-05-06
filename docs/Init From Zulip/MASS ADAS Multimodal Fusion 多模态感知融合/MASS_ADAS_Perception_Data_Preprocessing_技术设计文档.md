# MASS_ADAS Data Preprocessing 数据预处理模块技术设计文档

**文档编号：** SANGO-ADAS-DPP-001  
**版本：** 0.1 草案  
**日期：** 2026-04-26  
**编制：** 系统架构组  
**状态：** 开发参考

---

## 目录

1. 概述与职责定义
2. 输入与输出接口
3. 核心参数数据库
4. 船长的传感器信息整合思维模型
5. 计算流程总览
6. 步骤一：传感器数据接收与协议解析
7. 步骤二：时间戳同步与统一
8. 步骤三：坐标系对齐与空间校准
9. 步骤四：数据质量检测与标记
10. 步骤五：单传感器噪声滤波
11. 步骤六：数据格式标准化与输出
12. 雷达数据预处理详解
13. AIS 数据预处理详解
14. 摄像头数据预处理详解
15. 导航传感器数据预处理详解
16. LiDAR 数据预处理详解（可选）
17. 传感器时钟校准与 PTP/NTP
18. 安装偏差补偿（Boresight Calibration）
19. 传感器故障检测与隔离
20. 内部子模块分解
21. 数值算例
22. 参数总览表
23. 与其他模块的协作关系
24. 测试策略与验收标准
25. 参考文献与标准

---

## 1. 概述与职责定义

### 1.1 模块定位

Data Preprocessing（数据预处理）是多模态感知融合子系统（Perception Subsystem）的第一个子模块——所有传感器数据进入感知管线的入口。它接收来自雷达、AIS、摄像头、LiDAR（可选）和导航传感器的原始数据流，执行协议解析、时间同步、坐标对齐、质量检测和噪声滤波，输出统一格式的预处理数据供下游的目标检测与分类模块消费。

在真实船舶上，这些工作部分由硬件完成（如雷达的信号处理器），部分由导航综合系统的中间件完成（如 NMEA 解码），还有部分是操作员在脑中隐式完成的（如判断"这个雷达回波是不是杂波"）。在 MASS_ADAS 中，这些全部需要在软件中显式实现。

### 1.2 核心职责

- **协议解析：** 将各传感器的私有通信协议（NMEA 0183/2000、CANBus、以太网视频流等）解析为统一的内部数据结构。
- **时间同步：** 为所有传感器数据赋予统一的时间戳——不同传感器可能有不同的内部时钟，数据到达时刻可能有延迟，必须校准到同一时间基准。
- **坐标对齐：** 将所有传感器的空间参考从各自的安装坐标系（传感器坐标系）转换到统一的船体坐标系。
- **数据质量检测：** 检测无效数据、异常值、传感器故障的早期征兆。
- **噪声滤波：** 对原始数据做初步的噪声抑制——去除明显的杂波和异常点，但不做高级处理（那是后续模块的事）。
- **格式标准化：** 输出统一结构的数据包，无论来自哪个传感器，下游模块看到的数据格式完全一致。

### 1.3 设计原则

- **不丢数据原则：** 预处理只标记问题数据，不丢弃——让下游模块决定如何处理。异常数据标记为低置信度但仍然传递。
- **低延迟原则：** 预处理引入的延迟 < 50ms（雷达扫描周期通常 2~3 秒，AIS 报告间隔 2~30 秒，预处理延迟相对于传感器周期可忽略）。
- **时间精度原则：** 时间同步精度 < 10ms——这是多传感器融合的基本要求。如果传感器 A 的数据比传感器 B 晚 100ms 但时间戳标错了，融合时会产生空间对齐偏差。
- **传感器无关原则：** 输出格式与传感器类型无关。下游模块不需要知道一个目标检测来自雷达还是摄像头。

---

## 2. 输入与输出接口

### 2.1 输入——各传感器原始数据

| 传感器 | 通信协议 | 数据类型 | 典型频率 | 说明 |
|-------|---------|---------|---------|------|
| 海事雷达（X-band） | 以太网（Spoke data）或串口（ARPA 目标） | 方位-距离扫描数据 或 ARPA 跟踪目标 | 20~50 Hz（spoke）/ 2~4s（扫描周期） | 主要避碰传感器 |
| AIS 接收器 | NMEA 0183（串口/TCP） | !AIVDM/!AIVDO 语句 | 2~30s per target | 目标身份和运动信息 |
| 摄像头阵列 | GigE Vision / USB3 / RTSP | 图像帧 (RGB / IR) | 10~30 fps | 目标视觉检测 |
| LiDAR（可选） | 以太网（UDP 点云） | 3D 点云 | 10~20 Hz | 近距离精确测距 |
| GPS/GNSS | NMEA 0183 ($GPGGA, $GPRMC) | 位置、速度、时间 | 1~10 Hz | 本船定位 |
| 电罗经/卫星罗经 | NMEA 0183 ($HEHDT) | 真航向 | 10~50 Hz | 本船航向 |
| 计程仪 | NMEA 0183 ($VDVBW) | 对水速度 | 1~5 Hz | 本船速度 |
| IMU/MRU | 串口/以太网 | 横摇/纵摇/艏摇角速度/加速度 | 100~200 Hz | 运动补偿 |
| 风速风向仪 | NMEA 0183 ($WIMWV) | 风速、风向 | 1 Hz | 风偏流计算 |
| 测深仪 | NMEA 0183 ($SDDBT) | 水深 | 1~5 Hz | 水深监控 |

### 2.2 输出——统一格式的预处理数据

每类传感器的预处理输出通过独立的 ROS2 话题发布：

| 输出话题 | 消息类型 | 频率 | 说明 |
|---------|---------|------|------|
| `/perception/preprocessed/radar_targets` | RadarTargetArray | 2~4 Hz（每扫描周期一次） | 预处理后的雷达目标列表 |
| `/perception/preprocessed/radar_image` | RadarImage | 2~4 Hz | 预处理后的雷达扫描图像（供目标检测用） |
| `/perception/preprocessed/ais_targets` | AisTargetArray | 按接收频率 | 预处理后的 AIS 目标列表 |
| `/perception/preprocessed/camera_frames` | ImageArray | 10~30 Hz | 预处理后的摄像头图像帧 |
| `/perception/preprocessed/lidar_cloud` | PointCloud2 | 10~20 Hz | 预处理后的 LiDAR 点云（如有） |
| `/perception/preprocessed/own_ship` | OwnShipState | 50 Hz | 本船状态（位置、航向、速度、运动） |
| `/perception/preprocessed/environment` | EnvironmentState | 1 Hz | 环境状态（风、水深、能见度） |

**统一目标数据结构（RadarTarget / AisTarget 共享基类）：**

```
PreprocessedTarget:
    Header header                       # ROS2 标准头（含统一时间戳）
    uint32 sensor_id                    # 传感器标识
    string sensor_type                  # "radar" / "ais" / "camera" / "lidar"
    
    # ---- 空间信息（船体坐标系）----
    float64 range_m                     # 距本船距离（米）
    float64 bearing_deg                 # 相对方位（度，船首为 0°，顺时针）
    float64 x_body                      # 船体坐标系 x（前向，米）
    float64 y_body                      # 船体坐标系 y（右向，米）
    float64 latitude                    # WGS84 纬度（如可计算）
    float64 longitude                   # WGS84 经度
    
    # ---- 运动信息 ----
    float64 speed_mps                   # 速度（m/s，NaN=未知）
    float64 course_deg                  # 航向（度，NaN=未知）
    float64 heading_deg                 # 真航向（度，NaN=未知）
    
    # ---- 目标属性 ----
    float64 rcs_dbsm                    # 雷达截面积（dBsm，仅雷达）
    float64 length_m                    # 目标长度（米，NaN=未知）
    
    # ---- 质量指标 ----
    float64 measurement_confidence      # 测量置信度 [0,1]
    float64 timestamp_uncertainty_ms    # 时间戳不确定性（ms）
    uint8 quality_flags                 # 位标志：bit0=时间同步OK, bit1=坐标对齐OK, bit2=范围检查OK, bit3=无异常值
```

---

## 3. 核心参数数据库

### 3.1 时间同步参数

| 参数 | 值 | 说明 |
|------|-----|------|
| 系统时间基准 | UTC (PTP/NTP 同步) | 所有时间戳使用 UTC |
| 最大允许时钟偏差 | 50 ms | 超过此值则传感器需要重新校准 |
| 数据最大允许延迟 | 500 ms | 超过此延迟的数据标记为 stale |
| NMEA 解析时补偿延迟 | 根据波特率计算 | 串口传输延迟 = 消息长度/波特率 |
| 雷达 spoke 时间戳精度 | ±1 ms | 以太网雷达通常自带高精度时间戳 |
| AIS 消息时间戳精度 | ±2 s | AIS 时间来自 GPS UTC，报告有延迟 |
| 摄像头帧时间戳精度 | ±5 ms | 基于帧捕获中断 |

### 3.2 坐标系参数（传感器安装偏差）

每个传感器相对于船体坐标系原点（通常在重心或艏柱基线交点处）的安装位置和朝向：

```
struct SensorMount {
    string sensor_id;
    float64 x_offset;      # 船体前向偏移（米，正=向前）
    float64 y_offset;       # 船体右向偏移（米，正=向右）
    float64 z_offset;       # 船体上方偏移（米，正=向上）
    float64 heading_offset; # 方位偏差（度，正=顺时针）
    float64 pitch_offset;   # 俯仰偏差（度）
    float64 roll_offset;    # 横滚偏差（度）
};

# 示例——12m USV 的传感器布局
radar_mount:     {x: 4.0, y: 0.0, z: 3.5, heading: 0.0, pitch: 0, roll: 0}
ais_antenna:     {x: 5.0, y: 0.0, z: 4.0, heading: 0.0, pitch: 0, roll: 0}
camera_front:    {x: 5.5, y: 0.0, z: 2.5, heading: 0.0, pitch: -5, roll: 0}
camera_port:     {x: 3.0, y: -1.5, z: 2.5, heading: -90.0, pitch: -5, roll: 0}
camera_starboard:{x: 3.0, y: 1.5, z: 2.5, heading: 90.0, pitch: -5, roll: 0}
camera_aft:      {x: 1.0, y: 0.0, z: 2.5, heading: 180.0, pitch: -5, roll: 0}
gps_antenna:     {x: 4.5, y: 0.0, z: 4.5, heading: 0, pitch: 0, roll: 0}
imu:             {x: 3.0, y: 0.0, z: 1.0, heading: 0, pitch: 0, roll: 0}
```

### 3.3 数据质量阈值

| 传感器 | 异常检测阈值 | 说明 |
|-------|------------|------|
| 雷达距离 | < 10m 或 > 最大量程 | 近距离杂波或超量程 |
| 雷达方位 | N/A（全 360° 有效） | |
| AIS 速度 | > 50 节（25.7 m/s） | 不合理的高速 |
| AIS 位置 | 与上次位置差 > V_max × Δt × 2 | 位置跳变 |
| AIS 航向 | 0~360° 之外 | 无效值 |
| GPS HDOP | > 10 | 定位精度差 |
| 罗经航向 | 与 GPS COG 偏差 > 30°（高速时） | 可能故障 |
| 风速 | > 70 m/s（≈140 节） | 不合理 |
| 水深 | < 0 或 > 10000m | 无效值 |

---

## 4. 船长的传感器信息整合思维模型

### 4.1 船长如何处理多源信息

一个真实的船长在驾驶台上同时面对多个信息源——雷达屏幕、AIS 显示器、船外目视、VHF 无线电。他在脑中做的事情本质上就是数据预处理和融合：

**时间对齐：** 他看到雷达上一个目标出现在 45° 方位 3 海里处。他再看 AIS 显示器——有一艘货船报告在大约那个位置。但 AIS 的位置可能是 10 秒前报告的——在那 10 秒里目标可能移动了 100 多米。船长在脑中会自动做"它现在应该在更前面一点"的外推。这就是时间同步。

**空间对齐：** 雷达天线装在桅杆顶上，距船首 4 米。雷达测量的"距离"是从天线到目标的距离，而 AIS 显示的"距离"是从 GPS 天线到目标的距离。对于近距离目标（< 100m），这个几米的偏差可能有意义。船长不会有意识地计算这个偏差，但他在目测时会隐含地用"那个东西在我船首正前方"这样的感知来统一参考点。这就是坐标对齐。

**质量判断：** 船长看雷达时会自动过滤"杂波"——海面反射、雨杂波、侧瓣回波。经验丰富的船长一眼就能分辨"这个亮点是真目标还是假的"。他的判据是：亮点是否持续存在（杂波通常闪烁不定），形状是否合理（真目标通常有一定的方位展宽），位置是否跟 AIS 或目视一致。这就是质量检测。

**信息优先级：** 当多个信息源冲突时，船长有一个心理优先级：目视 > 雷达 > AIS（对于位置信息），AIS > 雷达 > 目视（对于身份信息）。这个优先级将在后续的 Multi-Sensor Fusion 模块中实现，但 Data Preprocessing 需要为每个数据源标注"质量等级"以支持这个优先级判断。

---

## 5. 计算流程总览

```
各传感器原始数据
    │ ┌──────────────────────────────────────┐
    ├─│ 雷达 (以太网/串口)                     │
    ├─│ AIS  (NMEA 0183)                      │
    ├─│ 摄像头 (GigE Vision/RTSP)              │
    ├─│ LiDAR (UDP 点云)                       │
    ├─│ GPS/GNSS (NMEA)                        │
    ├─│ 罗经/IMU (NMEA/串口)                   │
    └─│ 风速仪/测深仪 (NMEA)                   │
      └──────────────┬───────────────────────┘
                     │
                     ▼
    ┌──────────────────────────────────────┐
    │ 步骤一：协议解析                       │
    │ NMEA 解码、视频解码、点云解包           │
    └──────────────┬───────────────────────┘
                   │
                   ▼
    ┌──────────────────────────────────────┐
    │ 步骤二：时间戳同步                     │
    │ 统一到 UTC 时间基准                    │
    │ 补偿传感器延迟和传输延迟                │
    └──────────────┬───────────────────────┘
                   │
                   ▼
    ┌──────────────────────────────────────┐
    │ 步骤三：坐标系对齐                     │
    │ 传感器坐标系 → 船体坐标系 → WGS84      │
    └──────────────┬───────────────────────┘
                   │
                   ▼
    ┌──────────────────────────────────────┐
    │ 步骤四：数据质量检测                   │
    │ 范围检查、跳变检测、一致性检查          │
    └──────────────┬───────────────────────┘
                   │
                   ▼
    ┌──────────────────────────────────────┐
    │ 步骤五：噪声滤波                       │
    │ 雷达杂波抑制、AIS 位置平滑、图像增强    │
    └──────────────┬───────────────────────┘
                   │
                   ▼
    ┌──────────────────────────────────────┐
    │ 步骤六：标准化输出                     │
    │ 构建 PreprocessedTarget 统一结构       │
    │ 发布到 ROS2 话题                       │
    └──────────────────────────────────────┘
```

---

## 6. 步骤一：传感器数据接收与协议解析

### 6.1 NMEA 0183 解析

NMEA 0183 是航海电子设备的标准通信协议。大多数导航传感器（GPS、罗经、测深仪、风速仪）和 AIS 接收器通过此协议输出数据。

```
FUNCTION parse_nmea_sentence(raw_bytes):
    
    # NMEA 格式：$XXYYY,field1,field2,...*CC\r\n
    # XX = 设备类别标识（GP=GPS, HE=航向, AI=AIS 等）
    # YYY = 语句类型
    # CC = 校验和（XOR）
    
    # 1. 校验和验证
    payload = raw_bytes between '$' and '*'
    checksum_received = hex(raw_bytes after '*')
    checksum_calculated = XOR of all bytes in payload
    
    IF checksum_received != checksum_calculated:
        log_event("nmea_checksum_error", {raw: raw_bytes})
        RETURN {valid: false}
    
    # 2. 解析字段
    fields = payload.split(',')
    sentence_type = fields[0]    # 如 "$GPGGA", "$HEHDT", "$AIVDM"
    
    # 3. 按语句类型分派
    SWITCH sentence_type:
        CASE "$GPGGA":  RETURN parse_gga(fields)     # GPS 定位
        CASE "$GPRMC":  RETURN parse_rmc(fields)     # GPS 推荐最低信息
        CASE "$HEHDT":  RETURN parse_hdt(fields)     # 真航向
        CASE "$VDVBW":  RETURN parse_vbw(fields)     # 对水速度
        CASE "$SDDBT":  RETURN parse_dbt(fields)     # 水深
        CASE "$WIMWV":  RETURN parse_mwv(fields)     # 风速风向
        CASE "!AIVDM": RETURN parse_ais_vdm(fields) # AIS 消息
        CASE "!AIVDO": RETURN parse_ais_vdo(fields) # 本船 AIS
        DEFAULT:        RETURN {type: "unknown", raw: raw_bytes}
```

### 6.2 AIS 消息解码

AIS 消息的编码复杂度远高于普通 NMEA——有效载荷使用 6-bit ASCII 装甲编码，需要先解码为二进制位串再按消息类型解析：

```
FUNCTION parse_ais_vdm(fields):
    
    # 多句组装（AIS 消息可能跨多句）
    fragment_count = int(fields[1])
    fragment_number = int(fields[2])
    sequential_id = fields[3]
    channel = fields[4]    # "A" 或 "B"
    payload_armored = fields[5]
    
    IF fragment_count > 1:
        # 存入缓冲区等待全部分片
        buffer[sequential_id].append(payload_armored)
        IF len(buffer[sequential_id]) < fragment_count:
            RETURN {complete: false}
        payload_armored = ''.join(buffer[sequential_id])
        del buffer[sequential_id]
    
    # 6-bit ASCII 解码
    bits = decode_6bit_ascii(payload_armored)
    
    # 消息类型
    msg_type = bits[0:6] as uint
    
    SWITCH msg_type:
        CASE 1, 2, 3:    # 位置报告（A 类设备）
            RETURN parse_ais_position_report(bits)
        CASE 5:           # 静态和航次相关信息
            RETURN parse_ais_static_data(bits)
        CASE 18:          # 标准 B 类位置报告
            RETURN parse_ais_class_b_report(bits)
        CASE 21:          # 助航设备报告
            RETURN parse_ais_aton_report(bits)
        DEFAULT:
            RETURN {type: msg_type, raw_bits: bits}

FUNCTION parse_ais_position_report(bits):
    RETURN {
        mmsi:           bits[8:38] as uint,
        nav_status:     bits[38:42] as uint,
        rot:            bits[42:50] as signed / 4.733,    # 度/分钟
        sog:            bits[50:60] as uint / 10.0,       # 节
        position_accuracy: bits[60],
        longitude:      bits[61:89] as signed / 600000.0, # 度
        latitude:       bits[89:116] as signed / 600000.0,
        cog:            bits[116:128] as uint / 10.0,     # 度
        heading:        bits[128:137] as uint,             # 度（511=不可用）
        timestamp:      bits[137:143] as uint              # UTC 秒
    }
```

### 6.3 雷达数据接收

海事雷达的数据接口因厂商而异。常见的有两种模式：

**模式 A：ARPA 目标输出（NMEA TTM 语句）**

雷达内部已经完成了目标检测和跟踪，输出的是结构化的目标列表。

```
FUNCTION parse_ttm(fields):
    # $RATTM,nn,dd.dd,bbb.b,T,ss.ss,ccc.c,T,xx.xx,T,name,status,ref*CC
    RETURN {
        target_number: int(fields[1]),
        range_nm:      float(fields[2]),
        bearing_deg:   float(fields[3]),    # 真方位或相对方位
        bearing_ref:   fields[4],           # "T"=真方位, "R"=相对方位
        speed_knots:   float(fields[5]),
        course_deg:    float(fields[6]),
        cpa_nm:        float(fields[8]),     # 雷达内部计算的 CPA
        tcpa_min:      float(fields[9]),     # 雷达内部计算的 TCPA
        target_name:   fields[10],
        target_status: fields[11]            # "L"=丢失, "Q"=查询中, "T"=跟踪中
    }
```

**模式 B：原始扫描数据（以太网 Spoke Data）**

雷达输出的是每个方位角的反射强度数据（spoke），需要自行做目标检测。这种模式数据量大但灵活性高。

```
FUNCTION receive_radar_spoke(udp_packet):
    # 格式因厂商而异——以 Navico/B&G 为例
    header = parse_spoke_header(udp_packet)
    
    RETURN {
        azimuth_deg:    header.azimuth / 4096 × 360,
        range_cells:    header.range_data,        # 每个距离单元的回波强度
        range_per_cell: header.range_scale / header.num_cells,    # 每个单元的距离分辨率
        timestamp:      header.timestamp,
        spoke_index:    header.spoke_number
    }
```

### 6.4 摄像头帧接收

```
FUNCTION receive_camera_frame(camera_id):
    
    # GigE Vision 或 USB3 接口
    frame = camera_driver.grab_frame()
    
    IF frame IS NULL:
        log_event("camera_frame_dropped", {camera: camera_id})
        RETURN NULL
    
    RETURN {
        camera_id: camera_id,
        image: frame.data,
        width: frame.width,
        height: frame.height,
        format: frame.format,     # "BGR8", "MONO8", "IR16"
        timestamp: frame.timestamp,
        exposure_us: frame.exposure_time,
        gain_db: frame.gain
    }
```

---

## 7. 步骤二：时间戳同步与统一

### 7.1 时间同步的必要性

不同传感器的数据到达系统的时刻不等于数据"产生"的时刻。各种延迟来源：

| 延迟来源 | 典型值 | 说明 |
|---------|-------|------|
| 传感器采集延迟 | 1~50 ms | 传感器内部从测量到输出的延迟 |
| 通信传输延迟 | 1~100 ms | 串口/以太网传输延迟 |
| 协议编码延迟 | 1~10 ms | NMEA 串行化延迟 |
| 操作系统调度延迟 | 0.1~10 ms | ROS2 消息发布/接收延迟 |
| AIS 特殊延迟 | 0~30 秒 | AIS 报告间隔导致的数据老化 |

### 7.2 时间戳赋予策略

```
FUNCTION assign_unified_timestamp(sensor_data, sensor_config):
    
    # 策略 1：传感器自带高精度时间戳（如 PTP 同步的雷达、摄像头）
    IF sensor_config.has_hardware_timestamp:
        t_measurement = sensor_data.hardware_timestamp
        t_unified = t_measurement    # 直接使用
        uncertainty = 1 ms
    
    # 策略 2：传感器自带 GPS UTC 时间（如 AIS 消息中的 timestamp 字段）
    ELIF sensor_config.has_gps_time:
        t_gps = sensor_data.gps_utc_second
        t_unified = reconstruct_utc(t_gps)
        uncertainty = 2000 ms    # AIS 时间精度只到秒级，且报告有延迟
        
        # AIS 额外补偿：根据 MMSI 的报告间隔估计数据老化
        t_data_age = estimate_ais_data_age(sensor_data.mmsi, sensor_data.nav_status)
        t_unified -= t_data_age
        uncertainty += t_data_age × 1000    # 增大不确定性
    
    # 策略 3：无传感器时间戳——使用接收时间减去估计延迟
    ELSE:
        t_receive = now()
        t_estimated_delay = sensor_config.typical_delay_ms
        t_unified = t_receive - t_estimated_delay / 1000
        uncertainty = t_estimated_delay    # 不确定性等于估计延迟
    
    RETURN {timestamp: t_unified, timestamp_uncertainty_ms: uncertainty}
```

### 7.3 AIS 数据老化估计

AIS A 类设备的报告间隔取决于导航状态和速度：

```
FUNCTION estimate_ais_data_age(mmsi, nav_status, sog):
    
    # IMO 要求的最大报告间隔：
    IF nav_status IN [1, 5, 6]:    # 锚泊或系泊
        max_interval = 180    # 3 分钟
    ELIF sog < 3:
        max_interval = 10     # 10 秒
    ELIF sog < 14:
        max_interval = 6      # 6 秒（转弯时 2 秒）
    ELIF sog < 23:
        max_interval = 6
    ELSE:
        max_interval = 2      # 2 秒
    
    # 保守估计：假设数据老化 = 最大间隔的一半
    estimated_age = max_interval / 2
    
    RETURN estimated_age    # 秒
```

---

## 8. 步骤三：坐标系对齐与空间校准

### 8.1 传感器坐标系 → 船体坐标系

```
FUNCTION sensor_to_body(measurement, sensor_mount, own_ship_motion):
    
    # ---- 方位修正 ----
    # 雷达/LiDAR 测量的方位是相对于传感器安装朝向的
    bearing_body = measurement.bearing + sensor_mount.heading_offset
    bearing_body = normalize_0_360(bearing_body)
    
    # ---- 距离修正 ----
    # 传感器安装位置偏离船体原点导致的距离修正
    # 对于近距离目标（< 100m）才有意义
    IF measurement.range < 200:
        # 将目标位置从传感器坐标系转到船体坐标系
        target_x_sensor = measurement.range × cos(measurement.bearing × π/180)
        target_y_sensor = measurement.range × sin(measurement.bearing × π/180)
        
        target_x_body = target_x_sensor + sensor_mount.x_offset
        target_y_body = target_y_sensor + sensor_mount.y_offset
        
        range_body = sqrt(target_x_body² + target_y_body²)
        bearing_body = atan2(target_y_body, target_x_body) × 180/π
    ELSE:
        range_body = measurement.range    # 远距离目标偏移可忽略
    
    # ---- 运动补偿（如果传感器数据是在船体运动中采集的）----
    IF sensor_mount.requires_motion_compensation:
        # 补偿采集期间的横摇和纵摇
        range_body, bearing_body = apply_motion_compensation(
            range_body, bearing_body,
            sensor_mount.z_offset,
            own_ship_motion.roll,
            own_ship_motion.pitch
        )
    
    RETURN {range: range_body, bearing: bearing_body, x: target_x_body, y: target_y_body}
```

### 8.2 船体坐标系 → WGS84 地理坐标

```
FUNCTION body_to_geo(target_body, own_ship_position, own_ship_heading):
    
    # 船体坐标系中目标的前向和右向分量
    dx_body = target_body.x    # 前向（米）
    dy_body = target_body.y    # 右向（米）
    
    # 旋转到 ENU 坐标系
    hdg_rad = own_ship_heading × π / 180
    dx_enu = dx_body × sin(hdg_rad) + dy_body × cos(hdg_rad)    # 东向
    dy_enu = dx_body × cos(hdg_rad) - dy_body × sin(hdg_rad)    # 北向
    
    # ENU 偏移转到经纬度
    R = 6371000
    target_lat = own_ship_position.latitude + dy_enu / R × (180/π)
    target_lon = own_ship_position.longitude + dx_enu / (R × cos(own_ship_position.latitude × π/180)) × (180/π)
    
    RETURN {latitude: target_lat, longitude: target_lon}
```

### 8.3 运动补偿

在波浪中船体有横摇和纵摇运动。安装在桅杆顶部的雷达天线的运动幅度更大（杠杆臂效应），导致雷达测量的方位和距离有误差。

```
FUNCTION apply_motion_compensation(range, bearing, sensor_height, roll, pitch):
    
    # 横摇引起的方位误差
    # 传感器因横摇产生的侧向位移 = sensor_height × tan(roll)
    lateral_displacement = sensor_height × tan(roll)
    
    # 方位修正（近距离目标影响更大）
    IF range > 10:
        bearing_correction = atan2(lateral_displacement, range) × 180/π
        bearing -= bearing_correction
    
    # 纵摇引起的距离误差（通常很小，可忽略）
    
    RETURN {range, bearing}
```

---

## 9. 步骤四：数据质量检测与标记

### 9.1 范围检查

```
FUNCTION check_data_range(data, sensor_type):
    
    quality_flags = 0xFF    # 初始全部合格
    
    SWITCH sensor_type:
        CASE "radar":
            IF data.range < 10 OR data.range > RADAR_MAX_RANGE:
                quality_flags &= ~BIT_RANGE_OK
            IF data.rcs < RADAR_MIN_RCS:
                quality_flags &= ~BIT_RANGE_OK    # 回波太弱
        
        CASE "ais":
            IF data.sog > 50 × 0.5144:    # > 50 节
                quality_flags &= ~BIT_RANGE_OK
            IF abs(data.latitude) > 90 OR abs(data.longitude) > 180:
                quality_flags &= ~BIT_RANGE_OK
            IF data.cog < 0 OR data.cog > 360:
                quality_flags &= ~BIT_RANGE_OK
            IF data.heading == 511:    # AIS "不可用" 值
                data.heading = NaN
        
        CASE "gps":
            IF data.hdop > 10:
                quality_flags &= ~BIT_RANGE_OK
            IF abs(data.latitude) > 85:    # 极区 GPS 精度差
                data.measurement_confidence *= 0.5
    
    RETURN quality_flags
```

### 9.2 跳变检测

```
FUNCTION check_jump(current_value, previous_value, max_change_rate, dt, sensor_type):
    
    IF previous_value IS NULL:
        RETURN true    # 第一个数据点，无法判断
    
    change = abs(current_value - previous_value)
    max_expected = max_change_rate × dt × 2    # 允许 2 倍余量
    
    IF change > max_expected:
        log_event("data_jump_detected", {
            sensor: sensor_type,
            change: change,
            max_expected: max_expected
        })
        RETURN false    # 跳变——标记为低质量
    
    RETURN true
```

### 9.3 交叉一致性检查

```
FUNCTION check_cross_consistency(gps_position, compass_heading, speed_log, dt):
    
    issues = []
    
    # GPS 速度 vs 计程仪速度
    IF abs(gps_sog - speed_log_speed) > 2.0:    # > 2 m/s 差异
        issues.append("GPS-speedlog divergence")
    
    # GPS COG vs 罗经航向（高速时应接近一致）
    IF gps_sog > 3.0:    # 只在 > 3 m/s 时检查
        cog_heading_diff = abs(normalize_pm180(gps_cog - compass_heading))
        IF cog_heading_diff > 15:
            issues.append(f"GPS COG vs compass divergence: {cog_heading_diff:.0f}°")
    
    # GPS 位置推算 vs 实际
    IF previous_gps IS NOT NULL:
        predicted_lat = previous_gps.lat + gps_sog × cos(gps_cog) × dt / 111111
        predicted_lon = previous_gps.lon + gps_sog × sin(gps_cog) × dt / (111111 × cos(gps_lat))
        prediction_error = geo_distance(predicted_lat, predicted_lon, gps_lat, gps_lon)
        IF prediction_error > 50:    # > 50m 预测偏差
            issues.append(f"GPS position prediction error: {prediction_error:.0f}m")
    
    RETURN issues
```

---

## 10. 步骤五：单传感器噪声滤波

### 10.1 雷达杂波抑制

雷达原始数据中的杂波类型及处理：

```
FUNCTION filter_radar_clutter(radar_data, sea_state, rain_intensity):
    
    # ---- 海杂波（Sea Clutter）----
    # 近距离海面反射，强度随距离的立方衰减
    IF radar_data.range < SEA_CLUTTER_RANGE(sea_state):
        # STC（Sensitivity Time Control）——近距离增益自动降低
        attenuation = compute_stc_attenuation(radar_data.range, sea_state)
        radar_data.echo_strength -= attenuation
        
        IF radar_data.echo_strength < DETECTION_THRESHOLD:
            radar_data.is_clutter = true
    
    # ---- 雨杂波（Rain Clutter）----
    IF rain_intensity > 0:
        # FTC（Fast Time Constant）——微分处理，突出目标边缘
        radar_data = apply_ftc_filter(radar_data, ftc_level=rain_intensity)
    
    # ---- 干扰（Interference）----
    # 其他雷达的信号——表现为放射状条纹
    IF detect_interference_pattern(radar_data):
        radar_data = apply_interference_rejection(radar_data)
    
    RETURN radar_data

FUNCTION SEA_CLUTTER_RANGE(sea_state):
    # 海杂波的影响范围随海况增大
    RETURN {0: 500, 1: 1000, 2: 2000, 3: 3000, 4: 5000, 5: 8000}[min(sea_state, 5)]    # 米
```

### 10.2 AIS 位置平滑

```
FUNCTION smooth_ais_position(ais_target, history):
    
    IF len(history) < 2:
        RETURN ais_target    # 不够数据做滤波
    
    # AIS 位置更新间隔长（2~30 秒），位置可能有 GPS 噪声跳变
    # 使用一阶低通滤波
    α = 0.5    # AIS 数据点少，不能太重的滤波
    
    ais_target.latitude = α × ais_target.latitude + (1-α) × history[-1].latitude
    ais_target.longitude = α × ais_target.longitude + (1-α) × history[-1].longitude
    ais_target.sog = α × ais_target.sog + (1-α) × history[-1].sog
    ais_target.cog = circular_smooth(ais_target.cog, history[-1].cog, α)
    
    RETURN ais_target
```

### 10.3 摄像头图像预处理

```
FUNCTION preprocess_camera_frame(frame, camera_config):
    
    # 1. 去畸变
    undistorted = cv2.undistort(frame.image, camera_config.intrinsic, camera_config.distortion)
    
    # 2. 白平衡/自动曝光补偿（在强光/逆光环境中重要）
    corrected = apply_auto_exposure_correction(undistorted, frame.exposure_us)
    
    # 3. 如果是红外摄像头，做温度-灰度映射
    IF camera_config.type == "IR":
        corrected = apply_thermal_mapping(corrected, camera_config.temp_range)
    
    # 4. 图像增强（可选：雾天去雾、夜间增强）
    IF environment.visibility < 2.0:
        corrected = apply_dehazing(corrected)
    
    RETURN corrected
```

---

## 11. 步骤六：数据格式标准化与输出

### 11.1 统一数据包构建

```
FUNCTION build_preprocessed_target(sensor_data, sensor_type, body_coords, geo_coords, quality, timestamp_info):
    
    target = PreprocessedTarget()
    target.header.stamp = timestamp_info.timestamp
    target.sensor_id = sensor_data.sensor_id
    target.sensor_type = sensor_type
    
    # 空间信息
    target.range_m = body_coords.range
    target.bearing_deg = body_coords.bearing
    target.x_body = body_coords.x
    target.y_body = body_coords.y
    target.latitude = geo_coords.latitude
    target.longitude = geo_coords.longitude
    
    # 运动信息
    target.speed_mps = sensor_data.sog × 0.5144 IF sensor_type == "ais" ELSE NaN
    target.course_deg = sensor_data.cog IF sensor_type == "ais" ELSE NaN
    target.heading_deg = sensor_data.heading IF NOT NaN ELSE NaN
    
    # 质量指标
    target.measurement_confidence = compute_confidence(quality, sensor_type)
    target.timestamp_uncertainty_ms = timestamp_info.uncertainty
    target.quality_flags = quality
    
    RETURN target
```

### 11.2 本船状态构建

```
FUNCTION build_own_ship_state(gps, compass, speed_log, imu, wind, depth):
    
    state = OwnShipState()
    state.header.stamp = now()
    
    state.latitude = gps.latitude
    state.longitude = gps.longitude
    state.heading = compass.heading            # 真航向（度）
    state.cog = gps.cog                        # 对地航向（度）
    state.sog = gps.sog                        # 对地速度（m/s）
    state.stw = speed_log.speed                # 对水速度（m/s）
    state.roll = imu.roll                      # 横摇（度）
    state.pitch = imu.pitch                    # 纵摇（度）
    state.yaw_rate = imu.yaw_rate              # 艏摇角速度（度/秒）
    state.wind_speed = wind.speed              # 真风速（m/s）
    state.wind_direction = wind.direction      # 真风向（度）
    state.depth = depth.depth                  # 水深（米）
    state.hdop = gps.hdop                      # GPS 精度指标
    
    RETURN state
```

---

## 12. 雷达数据预处理详解

### 12.1 雷达数据流的两种模式

**模式 A（ARPA 目标列表）：** 雷达内部完成目标检测和跟踪，通过 NMEA TTM 语句输出跟踪目标。预处理只需做协议解析、时间同步、坐标对齐。优点是简单，缺点是依赖雷达内部算法——无法控制检测灵敏度。

**模式 B（原始扫描数据）：** 雷达输出每个方位角的反射强度（spoke data）。预处理需要做杂波抑制，目标检测由后续的 Object Detection 模块用 AI 算法完成。优点是灵活，缺点是数据量大（每秒数十 MB），计算负荷高。

**建议：** V1 版本使用模式 A（ARPA），同时采集原始数据用于 AI 模型训练。V2 版本切换到模式 B 用 AI 检测。

### 12.2 ARPA 目标补充信息

雷达 ARPA 通常不提供目标的 AIS 信息（MMSI、船名等）。但部分高端雷达支持 ARPA-AIS 关联显示。Data Preprocessing 不做关联——那是 Multi-Sensor Fusion 的职责。

### 12.3 雷达盲区和阴影

```
# 雷达天线有近距离盲区（通常 < 30~50m）
IF radar_target.range < RADAR_MIN_RANGE:
    radar_target.measurement_confidence = 0    # 近距离盲区内的目标不可靠

# 桅杆等结构物可能在某些方位造成雷达阴影（遮挡）
FOR EACH shadow_sector IN radar_shadow_sectors:
    IF radar_target.bearing IN shadow_sector:
        radar_target.measurement_confidence *= 0.3    # 阴影区内降低置信度

RADAR_MIN_RANGE = 30    # 米（需根据实际雷达型号确认）
```

---

## 13. AIS 数据预处理详解

### 13.1 AIS 消息类型优先级

不同类型的 AIS 消息包含不同的信息：

| 消息类型 | 内容 | 预处理动作 |
|---------|------|----------|
| 1/2/3 | A 类位置报告 | 提取 MMSI、位置、速度、航向、导航状态 |
| 5 | 静态和航次数据 | 提取船名、呼号、尺度、目的地、ETA |
| 18 | B 类位置报告 | 同 1/2/3 但精度较低 |
| 21 | 航标报告 | 提取航标位置和类型 |
| 14 | 安全相关广播 | 提取安全信息文本 |

### 13.2 AIS 数据完整性处理

```
FUNCTION validate_ais_data(ais_msg):
    
    # MMSI 验证
    IF ais_msg.mmsi < 100000000 OR ais_msg.mmsi > 799999999:
        # 非法 MMSI（不在 MID 范围内）
        ais_msg.measurement_confidence *= 0.3
    
    # 位置验证
    IF ais_msg.longitude == 181.0 OR ais_msg.latitude == 91.0:
        # AIS 默认"不可用"值
        ais_msg.latitude = NaN
        ais_msg.longitude = NaN
        ais_msg.measurement_confidence = 0
    
    # SOG 验证
    IF ais_msg.sog == 102.3:
        # AIS "不可用" 值（102.3 节）
        ais_msg.sog = NaN
    
    # COG 验证
    IF ais_msg.cog == 360.0:
        # AIS "不可用" 值
        ais_msg.cog = NaN
    
    # ROT 验证
    IF abs(ais_msg.rot) > 720:
        ais_msg.rot = NaN    # 超出合理范围
    
    RETURN ais_msg
```

### 13.3 AIS 位置外推

由于 AIS 报告间隔可能达 2~30 秒，收到的位置已经"过时"。需要用速度和航向外推到当前时刻：

```
FUNCTION extrapolate_ais_position(ais_msg, current_time):
    
    data_age = current_time - ais_msg.timestamp    # 秒
    
    IF data_age > MAX_AIS_EXTRAPOLATION_TIME:
        ais_msg.measurement_confidence *= 0.5
        data_age = MAX_AIS_EXTRAPOLATION_TIME    # 限制最大外推时间
    
    IF NOT isnan(ais_msg.sog) AND NOT isnan(ais_msg.cog) AND data_age > 1.0:
        # 匀速直线外推
        dx = ais_msg.sog × sin(ais_msg.cog × π/180) × data_age
        dy = ais_msg.sog × cos(ais_msg.cog × π/180) × data_age
        
        ais_msg.latitude += dy / 111111
        ais_msg.longitude += dx / (111111 × cos(ais_msg.latitude × π/180))
        
        ais_msg.timestamp_uncertainty_ms += data_age × 1000 × 0.1
        # 不确定性随外推时间线性增长
    
    RETURN ais_msg

MAX_AIS_EXTRAPOLATION_TIME = 60    # 秒——最多外推 1 分钟
```

---

## 14. 摄像头数据预处理详解

### 14.1 摄像头标定参数

每个摄像头需要标定以下参数：

```
struct CameraCalibration {
    # 内参
    float64[3][3] intrinsic_matrix;    # 焦距、主点
    float64[5] distortion_coeffs;      # 畸变系数（k1, k2, p1, p2, k3）
    
    # 外参（相对于船体坐标系）
    SensorMount mount;                 # 安装位置和朝向
    
    # 其他
    float64 fov_horizontal;            # 水平视场角（度）
    float64 fov_vertical;              # 垂直视场角（度）
    int resolution_w, resolution_h;    # 分辨率
};
```

### 14.2 图像坐标到方位角转换

目标检测模块会输出目标在图像中的像素坐标（bounding box）。预处理需要将像素坐标转换为方位角（相对于摄像头朝向）：

```
FUNCTION pixel_to_bearing(pixel_x, pixel_y, camera_calib):
    
    # 去畸变后的归一化坐标
    x_norm = (pixel_x - camera_calib.cx) / camera_calib.fx
    y_norm = (pixel_y - camera_calib.cy) / camera_calib.fy
    
    # 方位角（相对于摄像头光轴）
    azimuth_camera = atan2(x_norm, 1.0) × 180/π    # 度
    elevation_camera = atan2(-y_norm, 1.0) × 180/π  # 度（向下为正）
    
    # 转到船体坐标系
    azimuth_body = azimuth_camera + camera_calib.mount.heading_offset
    
    RETURN {azimuth: azimuth_body, elevation: elevation_camera}
```

### 14.3 摄像头无法提供距离

摄像头本身无法测量目标距离（单目摄像头）。距离信息需要通过以下方式获取：

```
# 方法 1：与雷达交叉定位（Multi-Sensor Fusion 处理）
# 方法 2：通过目标在水面的位置 + 摄像头高度几何估算
# 方法 3：通过 AI 模型从目标表观大小估算
# 方法 4：双目摄像头立体视差（如配备）

# 在 Data Preprocessing 阶段，摄像头目标的距离标记为 NaN
camera_target.range = NaN
```

---

## 15. 导航传感器数据预处理详解

### 15.1 GPS 数据处理

```
FUNCTION preprocess_gps(gps_raw):
    
    # GGA 语句包含定位质量指标
    fix_quality = gps_raw.fix_quality
    # 0=无定位, 1=标准GPS, 2=DGPS, 4=RTK固定, 5=RTK浮动
    
    confidence = {0: 0.0, 1: 0.7, 2: 0.85, 4: 0.99, 5: 0.95}[fix_quality]
    
    # HDOP 修正
    IF gps_raw.hdop > 5:
        confidence *= 0.5
    
    # 卫星数修正
    IF gps_raw.num_satellites < 4:
        confidence *= 0.3
    ELIF gps_raw.num_satellites < 6:
        confidence *= 0.7
    
    RETURN {
        latitude: gps_raw.latitude,
        longitude: gps_raw.longitude,
        altitude: gps_raw.altitude,
        hdop: gps_raw.hdop,
        fix_quality: fix_quality,
        num_satellites: gps_raw.num_satellites,
        confidence: confidence
    }
```

### 15.2 罗经数据处理

```
FUNCTION preprocess_compass(compass_raw, gps_cog, ship_speed):
    
    # 检查罗经值有效性
    IF compass_raw.heading < 0 OR compass_raw.heading > 360:
        RETURN {heading: NaN, confidence: 0}
    
    # 与 GPS COG 交叉验证（高速时有效）
    IF ship_speed > 3.0:
        diff = abs(normalize_pm180(compass_raw.heading - gps_cog))
        IF diff > 30:
            log_event("compass_gps_divergence", {diff: diff})
            # 可能罗经故障
            RETURN {heading: compass_raw.heading, confidence: 0.3}
    
    RETURN {heading: compass_raw.heading, confidence: 0.95}
```

---

## 16. LiDAR 数据预处理详解（可选）

### 16.1 点云预处理

```
FUNCTION preprocess_lidar(point_cloud):
    
    # 1. 去除无效点（距离 = 0 或超量程）
    valid_points = [p for p in point_cloud if 0.5 < p.range < LIDAR_MAX_RANGE]
    
    # 2. 去除水面反射点
    # 水面反射点通常在低仰角、近距离处
    non_water_points = [p for p in valid_points 
                        if p.elevation > WATER_SURFACE_THRESHOLD 
                        OR p.range > WATER_REFLECTION_RANGE]
    
    # 3. 运动补偿（船体运动导致的点云畸变）
    compensated = apply_lidar_motion_compensation(non_water_points, imu_data)
    
    # 4. 坐标转换到船体坐标系
    body_points = transform_to_body(compensated, lidar_mount)
    
    RETURN body_points

LIDAR_MAX_RANGE = 200    # 米（海事 LiDAR 典型量程）
WATER_SURFACE_THRESHOLD = -2°    # 仰角低于 -2° 的点可能是水面反射
WATER_REFLECTION_RANGE = 50      # 米——近距离水面反射范围
```

---

## 17. 传感器时钟校准与 PTP/NTP

### 17.1 时钟同步架构

```
系统时钟同步架构：

GPS 接收器 (PPS 1Hz) ─────────┐
                                │
NTP/PTP 服务器 ←────────────────┤
    │                           │
    ├──→ 主计算机系统时钟         │
    │       │                   │
    │       ├──→ 雷达（以太网PTP） │
    │       ├──→ 摄像头（PTP/NTP）│
    │       ├──→ LiDAR（PTP）    │
    │       │                   │
    │       └──→ ROS2 时钟       │
    │                           │
    └──→ 串口设备（NMEA 时间戳）  │
             (GPS/罗经/AIS)      │
                                │
GPS PPS 脉冲 ──────────────────┘
  提供 < 1μs 精度的时间基准
```

### 17.2 串口设备的延迟补偿

```
FUNCTION compute_serial_delay(baud_rate, message_length):
    # 每字节 = 1 start + 8 data + 1 parity + 1 stop = 11 bits
    bits_total = message_length × 11
    delay_s = bits_total / baud_rate
    RETURN delay_s

# 示例：4800 波特，80 字节 NMEA 语句
# delay = 80 × 11 / 4800 = 0.183 秒 ≈ 183 ms
# 这个延迟必须补偿！

# 38400 波特：delay = 80 × 11 / 38400 = 0.023 秒 ≈ 23 ms
# 建议 NMEA 设备使用 38400 或更高波特率
```

---

## 18. 安装偏差补偿（Boresight Calibration）

### 18.1 雷达方位校准

雷达天线的机械安装可能有小角度偏差（0.5°~3°），需要校准：

```
FUNCTION calibrate_radar_bearing(radar_targets, ais_targets):
    
    # 方法：找到雷达和 AIS 关联的同一目标，比较方位差
    
    bearing_offsets = []
    
    FOR EACH pair (radar, ais) IN matched_targets:
        offset = radar.bearing - ais.bearing    # 应该为零
        bearing_offsets.append(offset)
    
    IF len(bearing_offsets) >= 5:
        # 取中位数（鲁棒于异常值）
        calibration_offset = median(bearing_offsets)
        
        IF abs(calibration_offset) > 0.5:
            log_event("radar_bearing_calibration", {offset: calibration_offset})
            radar_mount.heading_offset += calibration_offset
    
    RETURN calibration_offset
```

### 18.2 摄像头外参校准

```
# 摄像头安装后的外参校准方法：
# 1. 在已知方位放置标定物（如航标）
# 2. 在图像中标注标定物的像素位置
# 3. 由已知方位和像素位置反算外参偏差
# 4. 更新 camera_mount 参数

# 这个校准应在系统安装后、首次航行前完成
# 之后每次维修/重新安装传感器后重新校准
```

---

## 19. 传感器故障检测与隔离

### 19.1 故障检测

```
FUNCTION detect_sensor_fault(sensor_id, data_stream):
    
    # 故障类型 1：数据停止
    IF time_since_last_data(sensor_id) > SENSOR_TIMEOUT:
        RETURN {fault: true, type: "data_loss", severity: "HIGH"}
    
    # 故障类型 2：数据全零或全满
    IF all_zeros(data_stream.last_10):
        RETURN {fault: true, type: "stuck_at_zero", severity: "HIGH"}
    
    # 故障类型 3：数据不变（传感器输出冻结）
    IF all_same(data_stream.last_20):
        RETURN {fault: true, type: "frozen", severity: "MEDIUM"}
    
    # 故障类型 4：校验和频繁失败
    IF checksum_failure_rate(sensor_id) > 0.3:
        RETURN {fault: true, type: "communication_error", severity: "MEDIUM"}
    
    # 故障类型 5：值持续超出物理合理范围
    IF out_of_range_rate(data_stream.last_50) > 0.5:
        RETURN {fault: true, type: "out_of_range", severity: "MEDIUM"}
    
    RETURN {fault: false}

SENSOR_TIMEOUT = {
    "radar": 5.0,        # 雷达 5 秒无数据
    "ais": 120.0,        # AIS 2 分钟无数据（正常间隔可达 30 秒）
    "camera": 2.0,       # 摄像头 2 秒无帧
    "gps": 5.0,          # GPS 5 秒
    "compass": 2.0,      # 罗经 2 秒
    "imu": 1.0           # IMU 1 秒
}
```

### 19.2 故障隔离

```
FUNCTION isolate_faulty_sensor(sensor_id, fault_info):
    
    # 将故障传感器标记为离线——不再使用其数据
    sensor_status[sensor_id] = "OFFLINE"
    
    # 通知下游模块
    publish_sensor_status(sensor_id, "OFFLINE", fault_info)
    
    # 通知 Shore Link
    report_to_shore("sensor_fault", {sensor: sensor_id, fault: fault_info})
    
    # 通知 Risk Monitor（传感器降级）
    notify_risk_monitor("sensor_degradation", sensor_id)
    
    log_event("sensor_isolated", {sensor: sensor_id, fault: fault_info})
```

---

## 20. 内部子模块分解

| 子模块 | 职责 | 建议语言 | 频率 |
|-------|------|---------|------|
| NMEA Parser | NMEA 0183/2000 协议解析 | C++ | 按接收频率 |
| AIS Decoder | AIS VDM 消息 6-bit 解码 + 消息解析 | C++ | 按接收频率 |
| Radar Interface | 雷达 spoke/ARPA 数据接收 | C++ | 20~50 Hz |
| Camera Interface | 摄像头帧采集 + 去畸变 | C++ | 10~30 Hz |
| LiDAR Interface | 点云接收 + 运动补偿 | C++ | 10~20 Hz |
| Timestamp Synchronizer | 时间戳统一 + 延迟补偿 | C++ | 所有传感器 |
| Coordinate Aligner | 传感器→船体→WGS84 坐标转换 | C++ | 所有传感器 |
| Quality Checker | 范围检查 + 跳变检测 + 一致性检查 | C++ | 所有传感器 |
| Noise Filter | 雷达杂波、AIS 平滑、图像增强 | C++ | 按数据类型 |
| Fault Detector | 传感器故障检测 + 隔离 | C++ | 1 Hz |
| Output Formatter | 构建统一输出数据包 | C++ | 按数据频率 |
| Boresight Calibrator | 安装偏差在线校准 | Python | 按需/离线 |

---

## 21. 数值算例

### 21.1 算例：AIS 时间同步与位置外推

```
收到 AIS 消息：
  MMSI = 123456789
  位置 = (31.2345°N, 121.4567°E)
  SOG = 12 节 = 6.17 m/s
  COG = 090°（正东）
  AIS 消息中的 timestamp 字段 = 42 秒（UTC 秒）
  
接收时刻 UTC = 14:35:48

数据老化估计：
  nav_status = 0（航行中），SOG = 12 节
  → 报告间隔最大 6 秒
  → estimated_age = 6/2 = 3 秒
  
  消息中 timestamp = 42 秒 → 采集时刻 ≈ 14:35:42
  接收时刻 = 14:35:48
  → 实际延迟 = 6 秒（与估计一致）

位置外推（6 秒）：
  dx = 6.17 × sin(90°) × 6 = 37.0 m（向东）
  dy = 6.17 × cos(90°) × 6 = 0 m
  
  经度修正 = 37.0 / (111111 × cos(31.23°)) = 37.0 / 95067 = 0.000389°
  
  外推后位置 = (31.2345°, 121.45709°)

时间戳不确定性 = 2000ms（AIS 基础）+ 6000ms × 0.1 = 2600 ms
```

---

## 22. 参数总览表

| 类别 | 参数 | 默认值 | 说明 |
|------|------|-------|------|
| **时间同步** | 最大时钟偏差 | 50 ms | |
| | 最大数据延迟 | 500 ms | 超过标记 stale |
| | AIS 最大外推时间 | 60 s | |
| **坐标** | 各传感器安装偏移 | 按实际 | 安装后校准 |
| | 运动补偿启用高度 | 1.0 m | z_offset > 此值启用 |
| **质量检测** | 雷达最小距离 | 30 m | 近距离盲区 |
| | AIS 最大速度 | 50 节 | 超过标记异常 |
| | GPS HDOP 上限 | 10 | 超过降低置信度 |
| | 罗经-COG 偏差上限 | 30° | 超过告警 |
| **噪声滤波** | AIS 平滑系数 | 0.5 | |
| | 雷达杂波 STC 参数 | 按海况 | |
| **故障检测** | 雷达超时 | 5 s | |
| | AIS 超时 | 120 s | |
| | 摄像头超时 | 2 s | |
| | GPS 超时 | 5 s | |
| | 校验和失败率阈值 | 30% | |
| **串口** | 建议波特率 | ≥ 38400 | 降低传输延迟 |

---

## 23. 与其他模块的协作关系

| 交互方 | 方向 | 数据 | 频率 |
|-------|------|------|------|
| 各传感器硬件 → Data Preproc | 输入 | 原始传感器数据 | 各传感器频率 |
| Data Preproc → Object Detection | 输出 | 预处理后的雷达/摄像头/LiDAR 数据 | 各传感器频率 |
| Data Preproc → Multi-Sensor Fusion | 输出 | 预处理后的 AIS 和 ARPA 目标 | 按数据频率 |
| Data Preproc → Nav Filter（如有独立 Nav Filter） | 输出 | GPS/罗经/IMU/计程仪数据 | 50 Hz |
| Data Preproc → Risk Monitor | 输出 | 传感器健康状态 | 1 Hz |
| Data Preproc → Shore Link | 输出 | 传感器故障报告 | 事件 |

---

## 24. 测试策略与验收标准

### 24.1 测试场景

| 场景 | 描述 | 验收标准 |
|------|------|---------|
| DPP-001 | NMEA 解析（GPS GGA） | 全部字段正确解析 |
| DPP-002 | NMEA 校验和错误 | 正确拒绝 + 日志 |
| DPP-003 | AIS 多句组装 | 正确组装 + 解码 |
| DPP-004 | AIS 位置外推（10 秒延迟） | 外推误差 < 10m |
| DPP-005 | 时间同步精度 | < 50ms（PTP 设备） |
| DPP-006 | 串口延迟补偿（4800 波特） | 补偿后时间误差 < 20ms |
| DPP-007 | 坐标对齐（近距离目标 50m） | 方位误差 < 1° |
| DPP-008 | 坐标对齐（远距离目标 5nm） | 方位误差 < 0.1° |
| DPP-009 | 数据跳变检测 | 100% 检测 > 阈值的跳变 |
| DPP-010 | GPS HDOP 高值处理 | 正确降低置信度 |
| DPP-011 | 雷达杂波抑制（海况 3） | 杂波目标标记率 > 80% |
| DPP-012 | 传感器故障——数据停止 | 超时正确检测 + 隔离 |
| DPP-013 | 传感器故障——数据冻结 | 正确检测 + 隔离 |
| DPP-014 | 多传感器同时运行（全部 6 类） | 总延迟 < 50ms |
| DPP-015 | 摄像头去畸变 | 畸变残差 < 1 像素 |

### 24.2 关键验收指标

| 指标 | 标准 |
|------|------|
| 预处理总延迟 | < 50 ms |
| 时间同步精度 | < 50 ms（PTP）/ < 200ms（NTP/串口） |
| 坐标对齐精度（方位） | < 1°（近距离）/ < 0.1°（远距离） |
| NMEA 解析正确率 | 100%（有效语句） |
| AIS 解码正确率 | 100%（完整消息） |
| 传感器故障检测率 | > 99% |
| 传感器故障误报率 | < 1% |

---

## 25. 参考文献与标准

| 编号 | 文献/标准 | 相关内容 |
|------|---------|---------|
| [1] | IEC 61162-1/2/3 | NMEA 0183 / NMEA 2000 / 以太网通信标准 |
| [2] | ITU-R M.1371-5 | AIS 技术特性（消息格式详解） |
| [3] | IEC 62388 | 海事雷达性能标准 |
| [4] | IEEE 1588 (PTP) | 精密时间同步协议 |
| [5] | OpenCV Documentation | 摄像头标定与去畸变 |
| [6] | IMO MSC.192(79) | INS 综合导航系统（多传感器集成要求） |
| [7] | IALA Guidelines on AIS | AIS 数据解释和使用指南 |

---

## v2.0 架构升级：反射弧传感器级触发

### A. 极近距离目标快速检测

在 v2.0 架构中，Data Preprocessing 增加了一个"极近距离快速检测"通道。该通道在正常的 Fusion Pipeline 之外并行运行——不等待完整的目标检测、融合和态势分析流程，而是在传感器数据预处理阶段就检测极近距离的异常回波/目标，直接触发反射弧的 Arm 级别。

```
FUNCTION reflex_quick_scan(preprocessed_data, sensor_type):
    
    alerts = []
    
    # ---- 雷达快速扫描 ----
    IF sensor_type == "radar":
        FOR EACH target IN preprocessed_data.radar_targets:
            IF target.range < REFLEX_SCAN_RANGE AND target.range_rate < -1.0:
                # 极近距离且正在接近
                alert = ReflexAlert()
                alert.range = target.range
                alert.bearing = target.bearing
                alert.closing_speed = abs(target.range_rate)
                alert.tcpa_estimate = target.range / alert.closing_speed
                alert.sensor_source = "radar"
                alert.consecutive_frames = count_consecutive(target.track_id)
                alert.first_detection = get_first_detection_time(target.track_id)
                alerts.append(alert)
    
    # ---- LiDAR 快速扫描 ----
    IF sensor_type == "lidar":
        # LiDAR 更新频率高（10-20Hz），适合近距离快速检测
        FOR EACH cluster IN preprocessed_data.lidar_clusters:
            IF cluster.range < REFLEX_SCAN_RANGE AND cluster.closing:
                alert = ReflexAlert()
                alert.range = cluster.range
                alert.bearing = cluster.bearing
                alert.sensor_source = "lidar"
                alert.consecutive_frames = count_consecutive_lidar(cluster.id)
                alerts.append(alert)
    
    # ---- 发布反射弧预警 ----
    IF len(alerts) > 0:
        reflex_alert_publisher.publish(alerts)    # → Risk Monitor 接收
        log_event("reflex_quick_scan_alert", {count: len(alerts), nearest: min(a.range for a in alerts)})
    
    RETURN alerts

REFLEX_SCAN_RANGE = 200    # 米——极近距离扫描范围
```

### B. 反射弧预警消息格式

```
ReflexAlert:
    Header header
    float64 range                       # 目标距离（米）
    float64 bearing                     # 目标方位（度）
    float64 closing_speed               # 接近速度（m/s）
    float64 tcpa_estimate               # 粗略 TCPA 估计（秒）
    string sensor_source                # "radar" / "lidar"
    uint32 consecutive_frames           # 连续检测帧数
    Time first_detection                # 首次检测时间
    bool closing                        # 是否正在接近

# 发布话题：/perception/reflex_alert
# QoS: RELIABLE, KEEP_LAST(1), 最高优先级
```

### C. 与正常 Perception 管线的关系

反射弧快速扫描与正常的 Object Detection → Multi-Sensor Fusion → Situational Awareness 管线**并行运行**，不替代也不干扰正常管线。

```
传感器原始数据
    │
    ├──→ [正常管线] Data Preproc → Object Detection → Fusion → SA → L3
    │                                                    (延迟: 100~3000ms)
    │
    └──→ [反射弧通道] Quick Scan → ReflexAlert → Risk Monitor → L5
                                                    (延迟: 20~100ms)
```

正常管线提供完整的目标信息（分类、速度、航向、MMSI）。反射弧通道只提供"极近距离有东西在接近"的粗略预警——足够触发紧急避让。

### D. 防误触发

```
# 反射弧快速扫描的误触发可能导致不必要的紧急停船
# 防误触发措施：

# 1. 不对单帧数据触发——至少需要 consecutive_frames >= 2
# 2. 雷达海杂波区域（近距离强杂波）自动提高阈值
IF sea_state >= 3 AND target.range < 100:
    # 大浪中 100m 内的回波很可能是海杂波
    REFLEX_SCAN_RANGE_EFFECTIVE = 100    # 缩小扫描范围
    required_consecutive = 3              # 需要 3 帧确认

# 3. 已知的固定物体（通过 ENC 匹配）不触发
IF target_position matches known_buoy OR known_structure:
    SKIP    # 航标和固定结构不触发反射弧

# 4. 反射弧触发后 Risk Monitor 的 ARMED 状态有 5 秒超时
# 如果 5 秒内没有 FIRE，自动解除
```

---

## v3.0 架构升级：传感器冗余投票与差值检测

### A. 冗余传感器配置

v3.0 架构要求关键传感器配置冗余：GNSS ×2~3、罗经 ×2~3、IMU ×1~2。Data Preprocessing 在将传感器数据传递给 Navigation Filter 之前，先做同类传感器之间的投票或差值检测。

### B. 三余度投票（Triple Redundancy Voting）

当同类传感器有三套时，使用中值投票——取三个值的中间值，自动排除偏差最大的那个：

```
FUNCTION triple_vote(sensor_1, sensor_2, sensor_3, threshold):
    
    values = sorted([sensor_1.value, sensor_2.value, sensor_3.value])
    median = values[1]    # 中间值
    
    # 检查每个传感器与中值的偏差
    FOR i, v IN enumerate([sensor_1.value, sensor_2.value, sensor_3.value]):
        deviation = abs(v - median)
        IF deviation > threshold:
            exclude_sensor(f"sensor_{i+1}", reason=f"voting outlier: deviation {deviation:.2f} > threshold {threshold}")
            sensor_status[f"sensor_{i+1}"] = "EXCLUDED"
        ELSE:
            sensor_status[f"sensor_{i+1}"] = "OK"
    
    RETURN median
```

### C. 双余度差值检测（Dual Redundancy Difference Test）

当同类传感器只有两套时，无法确定哪个对——只能检测两者是否一致：

```
FUNCTION dual_difference_test(sensor_1, sensor_2, threshold):
    
    diff = abs(sensor_1.value - sensor_2.value)
    
    IF diff > threshold:
        # 两个传感器偏差过大——无法确定哪个对
        log_event("sensor_divergence", {
            sensor_1: sensor_1.id, value_1: sensor_1.value,
            sensor_2: sensor_2.id, value_2: sensor_2.value,
            diff: diff
        })
        
        # 策略：选择历史质量更好的那个（如有质量评分），或选择精度指标更好的
        IF sensor_1.quality_score > sensor_2.quality_score:
            RETURN sensor_1.value
        ELSE:
            RETURN sensor_2.value
    ELSE:
        # 差值在范围内——取加权平均
        w1 = 1.0 / sensor_1.noise_estimate
        w2 = 1.0 / sensor_2.noise_estimate
        RETURN (sensor_1.value * w1 + sensor_2.value * w2) / (w1 + w2)
```

### D. 各传感器的投票/差值阈值

| 传感器 | 阈值 | 说明 |
|-------|------|------|
| GPS 位置 | 30 m（双）/ 50 m（三取中） | |
| 罗经航向 | 1.5°（双）/ 2.0°（三取中） | |
| IMU 横摇 | 1.0°（双） | |
| IMU 艏摇角速度 | 0.5 °/s（双） | |

### E. 投票结果传递给 Navigation Filter

投票/差值检测后的融合值作为 Navigation Filter EKF 的测量输入。被排除的传感器信息通过 `sensor_qualities` 字段传递给 Navigation Filter 和 Risk Monitor。

---

## v3.1 架构升级：反射弧快速扫描增加 PMS 预联动

### A. 反射弧 Arm 阶段的 PMS 预通知

v3.1 要求在反射弧从 IDLE 进入 ARMED 阶段时，不仅通知 Risk Monitor，还同步通知 Power Management System——让电力系统提前准备高负荷。

```
FUNCTION reflex_quick_scan_with_pms(preprocessed_data, sensor_type):
    
    alerts = reflex_quick_scan(preprocessed_data, sensor_type)    # 原有逻辑
    
    IF len(alerts) > 0 AND any(a.consecutive_frames >= 1 for a in alerts):
        
        # v3.1 新增：同步发送 PMS 预通知
        # 在 ReflexAlert 到达 Risk Monitor 之前，PMS 就已经收到预警
        # 这给了 PMS 额外的 2~5 秒准备时间（反射弧 Arm→Fire 的间隔）
        
        nearest_range = min(a.range for a in alerts)
        estimated_tcpa = nearest_range / max(a.closing_speed for a in alerts)
        
        pms_pre_alert = {
            type: "REFLEX_PRE_ALERT",
            estimated_power_kw: estimate_crash_stop_power(),
            estimated_time_to_fire_s: estimated_tcpa,
            action_required: "PREPARE_SPINNING_RESERVE"
        }
        
        pms_publisher.publish(pms_pre_alert)    # → Power Management System
        
        log_event("reflex_pms_pre_alert", {
            nearest_range: nearest_range,
            estimated_tcpa: estimated_tcpa
        })
    
    RETURN alerts

# 发布话题：/power/reflex_pre_alert
# QoS: RELIABLE, KEEP_LAST(1), 最高优先级
```

### B. 信号时序

```
T=0s    传感器检测到极近目标
T=0.02s Data Preprocessing 反射弧快速扫描检测到
        ├→ /perception/reflex_alert → Risk Monitor（原有）
        └→ /power/reflex_pre_alert → PMS（v3.1 新增）

T=0.5s  Risk Monitor 确认 → ARMED → 再次通知 PMS "ARMED"

T=1.0s  连续 2 帧确认 → FIRED → L5 执行紧急指令
        此时 PMS 已有 1 秒的准备时间
        → 备用发电机可能已启动（如果配备快速启动发电机）
        → 母排保护阈值已临时提高（允许短时过载）
```

---

## v3.1 补充升级：近场结构物检测与岸壁距离提取

### A. 问题——Object Detection 只识别船舶，不识别码头

当前 Object Detection 训练的 YOLOv8 模型针对的目标是"船舶"——类别包括 cargo/tanker/fishing/passenger/sailing。港内的码头面、防波堤、系泊桩、桥墩不在检测类别中。LiDAR 点云虽然能看到这些结构物，但没有专门的算法从点云中提取"最近的岸壁面"并输出距离。

### B. 近场结构物检测管线

```
LiDAR 点云 → 水面点滤除 → 结构物聚类 → 平面拟合 → 岸壁面提取 → 最近距离计算

FUNCTION detect_near_field_structures(lidar_points):
    
    # 1. 水面点滤除
    # 水面反射点在 z ≈ 0（海面）且反射率低
    above_water = [p for p in lidar_points if p.z > 0.5 or p.intensity > 0.3]
    
    # 2. 结构物聚类（DBSCAN）
    clusters = dbscan(above_water, eps=0.5, min_points=10)
    
    structures = []
    FOR EACH cluster IN clusters:
        
        # 3. 平面拟合（RANSAC）——码头面和防波堤通常是平面
        plane, inliers = ransac_plane_fit(cluster, distance_threshold=0.1)
        
        IF len(inliers) / len(cluster) > 0.7:
            # 大部分点属于平面——这是一个平面结构物（码头面/墙壁）
            structure_type = "wall"
            
            # 提取平面参数
            normal = plane.normal        # 法线方向
            distance = plane.distance    # 平面到原点距离
            extent = compute_extent(inliers)    # 结构物范围
            
        ELSE:
            # 非平面——可能是系泊桩、航标等圆柱/球形物体
            structure_type = "obstacle"
            center = mean(cluster.points)
            radius = max_distance_from_center(cluster)
        
        structures.append(NearFieldStructure(
            type=structure_type,
            points=cluster,
            plane=plane if structure_type == "wall" else None,
            center=center if structure_type == "obstacle" else None,
            min_distance=compute_min_distance_to_ship(cluster)
        ))
    
    # 4. 摄像头辅助分类
    # 将 LiDAR 检测到的结构物投影到摄像头画面上
    # 用摄像头确认结构物类型（码头 vs 船 vs 航标）
    FOR EACH structure IN structures:
        camera_roi = project_to_camera(structure.center)
        visual_class = camera_classifier.classify(camera_roi)
        structure.visual_class = visual_class    # "quay_wall" / "breakwater" / "moored_vessel" / "buoy"
    
    RETURN structures

# 发布话题：/perception/near_field_structures
# 消息类型：NearFieldStructureArray
# 频率：10 Hz（靠泊模式下）
```

### C. 岸壁距离输出

```
FUNCTION compute_quay_distances(structures, ship_outline):
    
    distances = QuayDistances()
    
    # 计算船体各关键点到最近结构物的距离
    distances.bow = min_distance(ship_outline.bow_point, structures)
    distances.stern = min_distance(ship_outline.stern_point, structures)
    distances.port_midship = min_distance(ship_outline.port_mid, structures)
    distances.starboard_midship = min_distance(ship_outline.stbd_mid, structures)
    
    # 最近点和方向
    closest = find_closest_point(ship_outline.all_points, structures)
    distances.min_distance = closest.distance
    distances.min_direction = closest.direction    # 从船到最近点的方向
    distances.min_point_ship = closest.ship_point  # 船上最近的点（船首/船舷/船尾）
    
    # 接触预测——按当前速度和方向，多少秒后接触
    IF approach_speed_toward(closest.direction) > 0:
        distances.time_to_contact = closest.distance / approach_speed_toward(closest.direction)
    ELSE:
        distances.time_to_contact = float('inf')    # 不在接近
    
    RETURN distances

# 发布话题：/perception/quay_distances
# 频率：10 Hz
# 供 L3 Risk Monitor 近场安全区 + L5 DP_APPROACH + HMI 显示
```

---

## 变更记录

| 版本 | 日期 | 作者 | 变更内容 |
|------|------|------|---------|
| 0.1 | 2026-04-26 | 架构组 | 初始版本 |
| 0.2 | 2026-04-26 | 架构组 | v2.0 升级：反射弧 |
| 0.3 | 2026-04-26 | 架构组 | v3.0 升级：传感器投票 |
| 0.4 | 2026-04-26 | 架构组 | v3.1 升级：PMS 预联动 |
| 0.5 | 2026-04-26 | 架构组 | v3.1 补充：增加近场结构物检测（LiDAR DBSCAN 聚类 + RANSAC 平面拟合 + 摄像头辅助分类）；增加岸壁距离提取（船体四方向 + 最近点 + 接触预测时间） |

---

**文档结束**
