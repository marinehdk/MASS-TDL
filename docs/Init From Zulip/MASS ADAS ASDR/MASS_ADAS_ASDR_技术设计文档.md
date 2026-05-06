# MASS_ADAS ASDR 智能黑匣子技术设计文档

**文档编号：** SANGO-ADAS-ASDR-001  
**版本：** 0.1 草案  
**日期：** 2026-04-26  
**编制：** 系统架构组  
**状态：** v2.0 防御性架构升级新增文档

---

## 目录

1. 概述与职责定义
2. 与传统 VDR 的关系
3. 记录范围定义
4. 数据存储架构
5. AI 语义日志格式
6. 事件触发永久存档
7. 数据安全与防篡改
8. 数据回放与审查工具
9. 岸基同步
10. 存储容量规划
11. 内部子模块分解
12. 参数总览表
13. 与其他模块的协作关系
14. 测试策略与验收标准
15. 参考文献

---

## 1. 概述与职责定义

### 1.1 模块定位

ASDR（Autonomous Ship Data Recorder，智能黑匣子）是 v2.0 防御性架构的 Z 轴底层定责基座。它扩展了传统 VDR（航行数据记录仪）的记录范围，增加了 AI 决策过程的完整语义记录——不仅记录"船做了什么"，还记录"AI 为什么这样做"。

### 1.2 解决的核心痛点

当发生碰撞事故或接近事故（Near Miss）时，海事法庭需要审查决策过程。对于传统船舶，审查的是船长的决策（通过 VDR 录音、航海日志、AIS 记录）。对于 MASS，审查的是 AI 系统的决策——但如果没有 ASDR，AI 的决策过程是黑盒：法官只能看到"AI 决定向右转 40°"，却不知道"为什么是 40°、为什么是向右、AI 考虑了哪些目标、排除了哪些方案"。

ASDR 让 AI 的所有操作"案底留痕"，为商业化定责提供闭环证据链。

---

## 2. 与传统 VDR 的关系

### 2.1 VDR 标准要求（IMO MSC.333(90)）

传统 VDR 必须记录的数据项：

| 数据项 | VDR 要求 | ASDR 扩展 |
|-------|---------|----------|
| 船位（GPS） | 是 | 是 + 增加定位精度和卫星数 |
| 航向（罗经） | 是 | 是 |
| 航速（SOG + STW） | 是 | 是 |
| 舵角 | 是 | 是 + 增加舵角指令来源（AI/人工/Checker 修正） |
| 主机转速/螺距 | 是 | 是 + 增加指令来源 |
| 水深 | 是 | 是 |
| 风速风向 | 是 | 是 |
| AIS 数据 | 是 | 是 |
| 雷达图像 | 是 | 是 + 增加 AI 检测标注 |
| VHF 无线电录音 | 是 | 不适用（MASS 通常无 VHF） |
| 桥楼音频 | 是 | 不适用（MASS 无人桥楼） |
| **AI 决策日志** | 不要求 | **ASDR 新增** |
| **Checker 否决日志** | 不要求 | **ASDR 新增** |
| **人工接管事件** | 不要求 | **ASDR 新增** |
| **传感器融合快照** | 不要求 | **ASDR 新增** |

### 2.2 ASDR 的定位

ASDR 是 VDR 的超集——它包含 VDR 的全部记录内容，并增加 AI 特有的语义日志。如果船舶同时安装了独立的 VDR 硬件（合规要求），ASDR 可以与 VDR 共享传感器数据源但独立存储。

---

## 3. 记录范围定义

### 3.1 分级记录策略

```
Level 1 — 全量高频（最近 30 分钟环形缓冲）：
  - 所有传感器原始数据（雷达扫描、摄像头帧、AIS 消息、GPS、IMU）
  - 所有控制指令（ψ_cmd, u_cmd, δ_cmd, RPM_cmd）
  - 所有控制器内部状态（PID 各项、积分器值）
  - Perception 融合目标列表（TrackedTargetArray）
  - COLREGs Engine 的逐目标决策详情
  - Checker 的每次校验结果
  - 记录频率：与各模块运行频率一致（50Hz/10Hz/2Hz）

Level 2 — 摘要中频（整个航次保留）：
  - 每秒一次的本船状态快照（位置、航向、航速、舵角、转速）
  - 每 2 秒一次的目标列表摘要（目标数、最近 CPA、最高威胁等级）
  - 每 30 秒一次的环境态势摘要（能见度、海况、交通密度）
  - 每次避碰事件的完整 ColregsDecision 和 AvoidanceCommand
  - 每次 Checker 否决的完整记录
  - 每次人工接管事件
  - 记录频率：1 Hz ~ 0.03 Hz

Level 3 — 事件触发永久存档：
  - 避碰事件前后各 5 分钟的 Level 1 全量数据
  - 人工接管事件前后各 5 分钟的 Level 1 全量数据
  - 紧急事件（EMERGENCY）前后各 5 分钟
  - Checker 否决事件前后各 2 分钟
  - 传感器故障事件前后各 2 分钟
  - 保留期：永久（直到人工清除）
```

### 3.2 AI 语义日志的具体内容

| 数据项 | 频率 | 来源模块 | 说明 |
|-------|------|---------|------|
| COLREGs 会遇分类 | 2 Hz | COLREGs Engine | 每个目标的分类结果和置信度 |
| COLREGs 责任判定 | 2 Hz | COLREGs Engine | 让路/直航/双方让路 + 适用规则列表 |
| COLREGs 行动推荐 | 2 Hz | COLREGs Engine | 推荐转向角度/速度变化 + 决策理由 |
| 多目标冲突消解 | 事件 | COLREGs Engine | 统一方案搜索过程和选择理由 |
| 避让航点生成 | 事件 | Avoidance Planner | 四航点序列 + 安全校验结果 |
| Risk Monitor 状态机转换 | 事件 | Risk Monitor | MONITORING→AVOIDING→ESCALATING→RESOLVED 全程记录 |
| CPA 趋势分析 | 2 Hz | Risk Monitor | CPA 线性回归斜率 + 趋势分类 |
| Target Tracker 威胁评分 | 2 Hz | Target Tracker | 每个目标的 4 因子评分明细 |
| Checker 校验结果 | 与各层同频 | Deterministic Checker | 每次校验的通过/否决 + 违反的规则 |
| LOS 引导输出 | 10 Hz | LOS Guidance | ψ_d + Δ + CTE |
| Heading Controller 输出 | 50 Hz | Heading Controller | P/I/D/FF 各项 + 舵角指令 |

---

## 4. 数据存储架构

### 4.1 环形缓冲（Level 1）

```
# 环形缓冲实现：固定大小的文件队列
# 最新数据覆盖最旧数据

ring_buffer_config:
    total_size: 32 GB               # Level 1 总存储
    segment_size: 1 GB              # 每个分段文件
    segment_count: 32               # 32 个分段 = 32GB
    recording_rate: ~1 GB/30min     # 全量数据约 1GB/30分钟
    retention: 30~60 分钟           # 取决于实际数据率

# 存储格式：SQLite 或 ROS2 bag (MCAP)
# 推荐 MCAP 格式——ROS2 原生支持，高效压缩，支持随机访问
```

### 4.2 航次摘要（Level 2）

```
voyage_summary_config:
    total_size: 2 GB per voyage     # 每航次约 2GB
    max_voyages_stored: 50          # 保留最近 50 个航次
    format: MCAP (compressed)       # LZ4 压缩
    index: SQLite                   # 快速查询索引
```

### 4.3 事件永久存档（Level 3）

```
event_archive_config:
    storage: 独立 SSD（防震、防水密封盒内）
    total_size: 256 GB              # 可存储约 250 个事件的 Level 1 数据
    format: MCAP + JSON 元数据
    encryption: AES-256-GCM         # 加密存储
    integrity: SHA-256 签名链       # 防篡改
    physical_protection: 
        - 耐冲击壳体（参考 VDR 标准 IEC 61996）
        - 防水（IPX8，水下 6000m / 30 天）
        - 耐高温（1100°C / 1 小时）
        - 可回收信标（如有）
```

### 4.4 存储硬件拓扑

```
┌──────────────────────────────────────┐
│ 主存储（OT 网络内的 NVMe SSD）        │
│   Ring Buffer (Level 1): 32 GB       │
│   Voyage Summary (Level 2): 100 GB   │
└──────────┬───────────────────────────┘
           │ 事件触发时复制
           ▼
┌──────────────────────────────────────┐
│ 事件存档（独立密封 SSD）              │
│   Event Archive (Level 3): 256 GB    │
│   防篡改 + 物理保护壳体               │
│   深水可回收（VDR 级别）              │
└──────────────────────────────────────┘
           │ 定期同步
           ▼
┌──────────────────────────────────────┐
│ 岸基备份（通过卫星通信上传）           │
│   Level 2 摘要：每航次结束后上传       │
│   Level 3 事件：事件发生后尽快上传     │
│   Level 1 全量：仅在港内 WiFi 下上传   │
└──────────────────────────────────────┘
```

---

## 5. AI 语义日志格式

### 5.1 决策记录结构

```
AiDecisionRecord:
    Time timestamp                      # 决策时刻
    string module                       # 产生决策的模块名
    string decision_type                # 决策类型
    
    # 输入快照（决策基于什么信息）
    OwnShipState own_ship_at_decision   # 本船状态快照
    TrackedTarget[] targets_at_decision # 目标列表快照
    EnvironmentState env_at_decision    # 环境快照
    
    # 决策内容
    string decision_summary             # 人类可读的决策摘要
    KeyValuePair[] decision_parameters  # 决策参数明细
    string[] rules_applied              # 应用的规则列表
    string[] alternatives_considered    # 被考虑但未选择的替代方案
    string selection_rationale          # 为什么选择了当前方案
    
    # Checker 校验结果
    bool checker_approved               # Checker 是否批准
    string checker_veto_reason          # 否决原因（如被否决）
    
    # 执行结果（下一周期回填）
    string execution_outcome            # "executed"/"modified"/"overridden"
```

### 5.2 示例日志条目

```json
{
    "timestamp": "2026-04-26T14:35:42.123Z",
    "module": "COLREGs_Engine",
    "decision_type": "avoidance_action",
    "own_ship": {"lat": 31.230, "lon": 121.474, "heading": 45.2, "speed": 8.1},
    "targets": [
        {"track_id": 42, "mmsi": 123456789, "cpa": 820, "tcpa": 340, "bearing": 56.3}
    ],
    "decision_summary": "交叉相遇，目标在右舷56°，本船为让路船，建议向右转55°",
    "rules_applied": ["Rule 15", "Rule 16", "Rule 8"],
    "alternatives_considered": [
        "右转40°: CPA=1650m < 目标2778m，不足",
        "右转50°: CPA=2600m < 目标2778m，不足",
        "右转55°: CPA=2850m ≥ 目标2778m，选择此方案"
    ],
    "selection_rationale": "右转55°是满足CPA≥CPA_safe×1.5的最小转向角度",
    "checker_approved": true
}
```

---

## 6. 事件触发永久存档

### 6.1 触发条件

```
FUNCTION check_archive_trigger(event):
    
    archive_triggers = [
        # 避碰事件
        event.type == "avoidance_started",
        event.type == "emergency_triggered",
        event.type == "escalation_triggered",
        
        # 人机交互事件
        event.type == "human_override_activated",
        event.type == "human_override_released",
        
        # Checker 否决事件
        event.type == "checker_veto" AND event.layer IN ["L2", "L3"],
        
        # 系统故障事件
        event.type == "sensor_failure",
        event.type == "actuator_failure",
        event.type == "software_crash",
        
        # 网络安全事件
        event.type == "intrusion_detected",
        event.type == "auth_failure_burst",
        
        # 接近事故
        event.type == "near_miss" AND event.cpa < CPA_safe × 0.5
    ]
    
    RETURN any(archive_triggers)
```

### 6.2 存档执行

```
FUNCTION execute_archive(trigger_event):
    
    # 从环形缓冲中提取事件前后 5 分钟的 Level 1 数据
    t_event = trigger_event.timestamp
    t_start = t_event - 300s    # 前 5 分钟
    t_end = t_event + 300s      # 后 5 分钟
    
    # 提取数据
    level1_data = ring_buffer.extract(t_start, t_end)
    
    # 构建事件包
    event_package = {
        event_id: generate_unique_id(),
        trigger: trigger_event,
        time_range: {start: t_start, end: t_end},
        data: level1_data,
        metadata: {
            ship_id: SHIP_IMO_NUMBER,
            voyage_id: current_voyage.task_id,
            archive_time: now()
        }
    }
    
    # 加密和签名
    encrypted = encrypt_aes256(event_package, asdr_key)
    signed = sign_sha256(encrypted, asdr_signing_key)
    
    # 写入事件存档 SSD
    event_archive.write(event_package.event_id, signed)
    
    # 尝试上传岸基（可能因带宽限制延迟）
    schedule_shore_upload(event_package, priority="HIGH")
    
    log_event("event_archived", {
        event_id: event_package.event_id,
        trigger: trigger_event.type,
        data_size: len(level1_data)
    })
```

---

## 7. 数据安全与防篡改

### 7.1 签名链

```
# 每个数据块（每秒一个）附带 SHA-256 哈希
# 每个块的哈希包含前一个块的哈希——形成链式签名

block_hash[n] = SHA256(data[n] || block_hash[n-1] || timestamp[n])

# 如果任何一个块被篡改，从被篡改的块开始，所有后续块的哈希都会不一致
# 审查时只需验证哈希链的连续性
```

### 7.2 访问控制

```
# ASDR 数据的访问权限分级

Level A（船端操作员）：只读——可查看最近 1 小时的 Level 2 摘要
Level B（岸基运营）：只读——可查看全部 Level 2 + Level 3
Level C（船级社/海事局）：只读——可查看全部数据（含 Level 1）
Level D（系统管理员）：读写——可清除过期数据（需双人授权）

# 数据删除策略：
# Level 1 环形缓冲：自动覆盖（最近 30 分钟）
# Level 2 航次摘要：保留 50 航次后自动清除最旧的
# Level 3 事件存档：永不自动删除——只有双人 Level D 授权可清除
```

---

## 8. 数据回放与审查工具

### 8.1 回放功能

```
# 基于 MCAP 格式的回放工具（可用 Foxglove Studio 或自研工具）

replay_features:
    - 时间轴浏览：拖动进度条查看任意时刻的态势
    - 多同步视图：同时显示雷达图、AIS 列表、AI 决策日志、控制指令
    - 变速回放：0.1x ~ 10x 速度
    - 事件跳转：点击事件列表直接跳到对应时刻
    - 标注工具：审查人员可在时间轴上添加标注和评论
    - 对比视图：显示 AI 的决策 vs Checker 的校验 vs 实际执行
    - 导出：生成 PDF 审查报告
```

### 8.2 自动分析

```
FUNCTION auto_analyze_event(event_package):
    
    # 自动生成事件分析报告
    report = EventReport()
    
    # 1. 时间线重建
    report.timeline = build_timeline(event_package)
    # 包含：每秒的本船状态、目标状态、AI 决策、Checker 校验、控制指令
    
    # 2. COLREGs 合规性审查
    report.colregs_compliance = analyze_colregs_compliance(event_package)
    # 逐条规则检查：是否正确分类、是否及时行动、是否大幅行动
    
    # 3. 系统响应时间分析
    report.response_times = analyze_response_times(event_package)
    # 从威胁检测到决策输出到执行开始的各环节延迟
    
    # 4. Checker 表现
    report.checker_performance = analyze_checker(event_package)
    # Checker 是否正确校验、有无遗漏、否决后的回退是否合理
    
    RETURN report
```

---

## 9. 岸基同步

```
FUNCTION sync_to_shore(data_type, data):
    
    SWITCH data_type:
        CASE "level2_summary":
            # 航次结束后通过卫星通信上传
            # 压缩后约 200KB~2MB/航次——卫星可承受
            upload_via_satellite(compress(data))
        
        CASE "level3_event":
            # 事件发生后优先上传
            # 压缩后约 50~200MB/事件——需要较好的通信条件
            IF satellite_bandwidth_sufficient:
                upload_via_satellite(compress(data))
            ELSE:
                queue_for_port_upload(data)    # 在港内用 WiFi 上传
        
        CASE "level1_full":
            # 全量数据只在港内上传（数据量太大）
            # 或通过物理方式取出 SSD
            queue_for_port_upload(data)
```

---

## 10. 存储容量规划

### 10.1 数据率估算

| 数据源 | 原始数据率 | Level 1 记录率 | 压缩后 |
|-------|----------|-------------|--------|
| 雷达扫描 | 5 MB/s | 5 MB/s | ~2 MB/s |
| 摄像头（4 路 720p） | 40 MB/s | 10 MB/s (降采样/抽帧) | ~3 MB/s |
| AIS | 0.01 MB/s | 0.01 MB/s | ~0.005 MB/s |
| 导航传感器 | 0.05 MB/s | 0.05 MB/s | ~0.02 MB/s |
| AI 决策日志 | 0.1 MB/s | 0.1 MB/s | ~0.05 MB/s |
| 控制指令 | 0.02 MB/s | 0.02 MB/s | ~0.01 MB/s |
| **Level 1 总计** | — | **~15 MB/s** | **~5 MB/s** |

### 10.2 存储容量需求

```
Level 1（30 分钟环形缓冲）：
  5 MB/s × 1800s = 9 GB → 分配 32 GB（余量充足）

Level 2（50 航次摘要）：
  ~50 KB/s × 平均 10 小时/航次 = 1.8 GB/航次 → 50 航次 = 90 GB → 分配 128 GB

Level 3（250 个事件）：
  每事件 10 分钟 Level 1 = 5 MB/s × 600s = 3 GB → 250 事件 = 750 GB → 分配 1 TB
  （但考虑实际发生频率——每年约 10~50 个存档事件——256 GB 足够数年使用）

总存储需求：
  主 SSD：160 GB（Level 1 + Level 2）
  事件 SSD：256 GB（Level 3）
  总计：~420 GB
```

---

## 11. 内部子模块分解

| 子模块 | 职责 | 建议语言 |
|-------|------|---------|
| Data Collector | 从各模块收集数据写入环形缓冲 | C++ |
| Ring Buffer Manager | Level 1 环形缓冲管理 | C++ |
| Summary Generator | Level 2 摘要生成（降采样+聚合） | C++ |
| Event Trigger | 事件检测 + Level 3 存档触发 | C++ |
| Archive Writer | Level 3 加密+签名+写入 | C++ |
| Integrity Checker | 哈希链验证 + 定期自检 | C++ |
| Shore Uploader | 岸基同步（卫星/WiFi） | Python |
| Replay Server | 数据回放服务（HTTP/WebSocket） | Python |
| Auto Analyzer | 事件自动分析报告生成 | Python |

---

## 12. 参数总览表

| 参数 | 值 | 说明 |
|------|-----|------|
| Level 1 缓冲大小 | 32 GB | 约 30 分钟 |
| Level 2 保留航次数 | 50 | |
| Level 3 存储大小 | 256 GB | |
| Level 1 数据率 | ~5 MB/s（压缩后） | |
| 事件存档窗口 | 前后各 5 分钟 | |
| 哈希算法 | SHA-256 | |
| 加密算法 | AES-256-GCM | |
| 事件存档物理保护 | IEC 61996（VDR 级别） | |
| 数据保留期（Level 3） | 永久 | |
| 岸基同步——摘要 | 航次结束后 | |
| 岸基同步——事件 | 尽快（带宽允许时） | |

---

## 13. 与其他模块的协作

| 交互方 | 方向 | 数据 |
|-------|------|------|
| 所有模块 → ASDR | 输入 | 各模块的运行数据和日志 |
| COLREGs Engine → ASDR | 输入 | AI 决策详情（语义日志） |
| Checker → ASDR | 输入 | 否决事件记录 |
| Risk Monitor → ASDR | 输入 | 避碰事件生命周期 |
| Override Arbiter → ASDR | 输入 | 人工接管事件 |
| IDS → ASDR | 输入 | 网络安全事件 |
| ASDR → Shore Link | 输出 | 摘要和事件数据上传 |
| ASDR → 回放工具 | 输出 | 数据回放和审查 |

---

## 14. 测试策略与验收标准

| 场景 | 描述 | 验收标准 |
|------|------|---------|
| ASDR-001 | Level 1 持续记录 30 分钟 | 数据完整、无丢帧 |
| ASDR-002 | 环形缓冲覆盖后数据一致性 | 最新 30 分钟数据完整 |
| ASDR-003 | 避碰事件触发 Level 3 存档 | 前后各 5 分钟完整提取 |
| ASDR-004 | 人工接管事件触发存档 | 同上 |
| ASDR-005 | 哈希链完整性验证 | 连续 1 小时数据链无断裂 |
| ASDR-006 | 数据篡改检测 | 修改任意块后哈希链断裂被检出 |
| ASDR-007 | 加密数据解密回放 | 正确解密 + 回放 |
| ASDR-008 | 岸基同步（Level 2） | 航次摘要完整上传 |
| ASDR-009 | 存储容量达 90% 时告警 | 正确告警 |
| ASDR-010 | 回放工具时间轴浏览 | 拖动到任意时刻正确显示 |
| ASDR-011 | 自动分析报告生成 | 报告包含时间线+合规性+响应时间 |
| ASDR-012 | Level 3 物理保护测试 | 跌落/浸水后数据可恢复 |

---

## 15. 参考文献

| 编号 | 文献 | 相关内容 |
|------|------|---------|
| [1] | IMO MSC.333(90) | VDR 性能标准 |
| [2] | IEC 61996 | VDR 物理保护和数据格式标准 |
| [3] | IMO MSC.467(101) | MASS 操作指南（数据记录要求） |
| [4] | MCAP Format Specification | ROS2 数据记录格式 |
| [5] | DNV-ST-0561 | 自主船舶标准（运行数据记录要求） |

---

**文档结束**
