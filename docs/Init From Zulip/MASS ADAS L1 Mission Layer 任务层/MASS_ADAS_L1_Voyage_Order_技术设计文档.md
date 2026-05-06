# MASS_ADAS Voyage Order 航次指令模块技术设计文档

**文档编号：** SANGO-ADAS-VOR-001  
**版本：** 0.1 草案  
**日期：** 2026-04-26  
**编制：** 系统架构组  

---

## 目录

1. 概述与职责定义
2. 输入与输出接口
3. 核心参数数据库
4. 船东/调度员的航次规划思维模型
5. 计算流程总览
6. 步骤一：航次指令接收与解析
7. 步骤二：航次指令完整性校验
8. 步骤三：航次可行性预检
9. 步骤四：航次参数补全与默认值填充
10. 步骤五：航次指令分解与下发
11. 航次指令格式定义
12. 航次修改与取消
13. 多航次队列管理
14. 岸基远程指令的认证与安全
15. 航次执行状态监控与报告
16. 紧急航次变更处理
17. 内部子模块分解
18. 数值算例
19. 参数总览表
20. 与其他模块的协作关系
21. 测试策略与验收标准
22. 参考文献与标准

---

## 1. 概述与职责定义

### 1.1 模块定位

Voyage Order（航次指令）是 MASS_ADAS 五层架构中 Layer 1（Mission Layer，任务层）的入口模块。它是整个系统的"任务起点"——接收来自岸基调度系统或船端操作员的航次指令（"从 A 港到 B 港，XX 时间出发，YY 时间到达"），对指令进行校验和补全后，下发给 L2 Voyage Planner 执行航线规划。

在传统航运中，航次指令对应的是船东或租家通过电子邮件或调度系统发给船长的"航次命令书（Voyage Order / Sailing Order）"——包含出发港、目的港、预计出发时间、要求到达时间、货物信息、特殊要求等。船长收到后审查可行性，然后开始航次计划。Voyage Order 模块就是这个"收到命令书并审查"过程的自动化。

### 1.2 核心职责

- **指令接收：** 从岸基调度系统（通过卫星通信或移动网络）或本地操作界面接收航次指令。
- **完整性校验：** 检查指令是否包含全部必要信息，格式是否正确。
- **可行性预检：** 做初步的可行性判断——距离是否合理、时间是否充裕、燃油是否足够。
- **参数补全：** 对指令中未指定的参数填充合理的默认值。
- **指令分解与下发：** 将高层航次指令转化为 L2 Voyage Planner 能处理的结构化输入。
- **执行监控：** 跟踪航次执行进度，向岸基报告状态。

### 1.3 设计原则

- **指令不信任原则：** 即使指令来自岸基，也不盲目执行。如果指令中的参数明显不合理（如要求的 ETA 物理上不可能达到），系统应拒绝并反馈原因。
- **人在回路原则：** 对于 MASS 自主等级较低的阶段（IMO Degree 2——远程控制），航次指令的执行需要岸基确认。自主等级提高后可逐步放开。
- **可追溯原则：** 每个航次指令的接收、校验、执行、完成全过程有完整日志，满足 ISM 审计要求。

---

## 2. 输入与输出接口

### 2.1 输入

| 输入项 | 来源 | 通信方式 | 说明 |
|-------|------|---------|------|
| 航次指令 | 岸基调度系统 | 卫星通信 / 4G/5G / WiFi | VoyageOrderMessage |
| 本地航次指令 | 船端操作界面 | 本地 HMI | 备用输入方式 |
| 航次修改指令 | 岸基 | 同上 | 修改已有航次的参数 |
| 航次取消指令 | 岸基 | 同上 | 取消当前航次 |
| 本船当前状态 | Nav Filter | ROS2 | 位置、航速、燃油 |
| 本船参数 | Parameter DB | 配置文件 | 最大速度、续航力、吃水 |

### 2.2 输出

| 输出项 | 接收方 | 话题/接口 | 说明 |
|-------|-------|----------|------|
| 结构化航次任务 | Weather Routing, ETA/Fuel Optimizer | `/mission/voyage_task` | 校验和补全后的航次参数 |
| 航次执行状态 | 岸基 | Shore Link | 接收/规划中/执行中/完成/异常 |
| 航次拒绝通知 | 岸基 | Shore Link | 指令不可行时的拒绝理由 |
| 航次进度报告 | 岸基 | Shore Link | 定期进度报告（ETA 更新等） |

---

## 3. 核心参数数据库

### 3.1 航次指令校验参数

| 参数 | 值 | 说明 |
|------|-----|------|
| 最大航次距离 | 500 nm | 超过本船续航力的航次拒绝 |
| 最短航次时间 | 全速航行时间 × 1.2 | 不可能在此时间内到达 |
| 最长航次时间 | 经济航速时间 × 3.0 | 时间过长可能不合理 |
| 最低出发燃油量 | 航次消耗 × 1.3 + 储备 | 至少 30% 余量 + 应急储备 |
| 最大允许吃水 | 根据目的港水深 | 确保目的港可进入 |
| 航次指令有效期 | 24 小时 | 超过 24 小时未执行的指令自动过期 |

### 3.2 默认值参数

| 指令字段 | 默认值 | 说明 |
|---------|-------|------|
| 优先级 | "fuel_optimal" | 默认燃油优化 |
| 计划航速 | V_econ（经济航速） | 默认经济航速 |
| 安全余量 | "standard" | 标准安全余量 |
| 气象规避 | "auto" | 自动气象规避 |
| 岸基通信间隔 | 30 分钟 | 每 30 分钟报告一次 |

---

## 4. 船东/调度员的航次规划思维模型

### 4.1 调度员如何下达航次指令

调度员（或船东运营部门）在下达航次指令时考虑的因素：

**商业因素：** 货物何时需要到达？延迟的经济代价是什么？提前到达是否有意义（比如目的港泊位还没空）？

**成本因素：** 燃油是最大的变动成本。全速航行可以提前到达但燃油消耗多，经济航速节油但耗时更长。调度员通常给出一个 ETA 窗口（不是精确时间），让系统在窗口内优化燃油消耗。

**安全因素：** 是否有台风路径需要避开？目的港有没有进港潮汐窗口限制？某些海峡是否有通行时间限制？

**合规因素：** 是否需要途经某个国家的领海（需要提前通报）？排放控制区（ECA）内是否需要切换低硫燃油？

### 4.2 航次指令的典型内容

一个完整的航次指令通常包含：

```
出发信息：
  出发港 / 出发泊位 / 出发锚地
  计划出发时间（UTC）
  引航要求

目的信息：
  目的港 / 目的泊位 / 目的锚地
  要求到达时间窗口（最早 ETA ~ 最晚 ETA）
  引航要求

航行参数：
  计划航速 或 优化目标（燃油优先 / 时间优先）
  气象规避策略
  特殊航线要求（如必须经过某个航路点、避开某个区域）

本船信息：
  当前吃水（前/后吃水）
  燃油存量
  特殊限制（如机器故障降低最大航速）

通信要求：
  报告间隔
  紧急联系渠道
```

---

## 5. 计算流程总览

```
岸基航次指令
      │
      ▼
┌──────────────────────────────────┐
│ 步骤一：指令接收与解析             │
│ 反序列化 + 认证验证                │
└────────────┬─────────────────────┘
             │
             ▼
┌──────────────────────────────────┐
│ 步骤二：完整性校验                │
│ 必填字段检查                      │
└────────────┬─────────────────────┘
             │
             ▼
┌──────────────────────────────────┐
│ 步骤三：可行性预检                │
│ 距离/时间/燃油/吃水初步校验       │
└────────────┬─────────────────────┘
             │ 不可行 → 拒绝 + 反馈原因
             │ 可行 ↓
             ▼
┌──────────────────────────────────┐
│ 步骤四：参数补全                  │
│ 填充默认值                        │
└────────────┬─────────────────────┘
             │
             ▼
┌──────────────────────────────────┐
│ 步骤五：指令分解与下发            │
│ 构建 VoyageTask → 下发 L2        │
└────────────┬─────────────────────┘
             │
             ▼
航次执行监控（持续）
```

---

## 6. 步骤一：航次指令接收与解析

### 6.1 指令消息格式

```
VoyageOrderMessage:
    # ---- 指令元信息 ----
    string order_id                     # 唯一指令 ID
    string issuer                       # 发出者标识（岸基调度系统 ID）
    Time issue_time                     # 发出时间（UTC）
    string auth_token                   # 认证令牌（防伪造）
    string order_type                   # "new" / "modify" / "cancel"
    
    # ---- 出发信息 ----
    string departure_port               # 出发港名称或 UN/LOCODE
    float64 departure_lat               # 出发点纬度
    float64 departure_lon               # 出发点经度
    Time planned_departure_time         # 计划出发时间（UTC）
    
    # ---- 目的信息 ----
    string destination_port             # 目的港名称或 UN/LOCODE
    float64 destination_lat             # 目的点纬度
    float64 destination_lon             # 目的点经度
    Time eta_earliest                   # 最早到达时间（UTC）
    Time eta_latest                     # 最晚到达时间（UTC）
    
    # ---- 航行参数 ----
    string optimization_priority        # "fuel_optimal" / "time_optimal" / "balanced"
    float64 planned_speed_knots         # 计划航速（节，0 = 系统自行决定）
    string weather_avoidance            # "none" / "auto" / "conservative"
    Waypoint[] mandatory_waypoints      # 必经航路点（可选）
    GeoFence[] exclusion_zones          # 必须避开的区域（可选）
    
    # ---- 本船信息 ----
    float64 draft_fore_m                # 前吃水（米）
    float64 draft_aft_m                 # 后吃水（米）
    float64 fuel_on_board_kg            # 当前燃油存量（千克）
    string[] special_restrictions       # 特殊限制（如 "max_speed_8kn"）
    
    # ---- 通信要求 ----
    float64 report_interval_min         # 报告间隔（分钟）
    string emergency_contact            # 紧急联系方式
```

### 6.2 指令接收

```
FUNCTION receive_voyage_order(message_bytes, source):
    
    # 1. 解析消息
    order = deserialize(message_bytes, VoyageOrderMessage)
    
    IF order IS NULL:
        log_event("order_parse_failed", {source, raw_length: len(message_bytes)})
        RETURN {accepted: false, reason: "消息格式错误，无法解析"}
    
    # 2. 认证验证
    IF NOT verify_auth_token(order.auth_token, order.issuer):
        log_event("order_auth_failed", {order_id: order.order_id, issuer: order.issuer})
        RETURN {accepted: false, reason: "认证失败，指令被拒绝"}
    
    # 3. 防重复
    IF order.order_id IN processed_orders:
        log_event("order_duplicate", {order_id: order.order_id})
        RETURN {accepted: false, reason: "重复指令"}
    
    log_event("order_received", {
        order_id: order.order_id,
        issuer: order.issuer,
        departure: order.departure_port,
        destination: order.destination_port,
        source: source
    })
    
    RETURN {accepted: true, order: order}
```

---

## 7. 步骤二：航次指令完整性校验

```
FUNCTION validate_order_completeness(order):
    
    missing_fields = []
    
    # 必填字段检查
    IF isnan(order.departure_lat) OR isnan(order.departure_lon):
        missing_fields.append("departure_position")
    
    IF isnan(order.destination_lat) OR isnan(order.destination_lon):
        missing_fields.append("destination_position")
    
    IF order.eta_latest IS NULL:
        missing_fields.append("eta_latest")
    
    IF order.draft_fore_m ≤ 0 OR order.draft_aft_m ≤ 0:
        missing_fields.append("draft")
    
    IF order.fuel_on_board_kg ≤ 0:
        missing_fields.append("fuel_on_board")
    
    IF len(missing_fields) > 0:
        RETURN {
            valid: false,
            reason: f"缺少必填字段：{', '.join(missing_fields)}"
        }
    
    # 逻辑一致性检查
    IF order.eta_earliest IS NOT NULL AND order.eta_earliest > order.eta_latest:
        RETURN {valid: false, reason: "最早到达时间晚于最晚到达时间"}
    
    IF order.planned_departure_time > order.eta_latest:
        RETURN {valid: false, reason: "出发时间晚于最晚到达时间"}
    
    RETURN {valid: true}
```

---

## 8. 步骤三：航次可行性预检

### 8.1 距离可行性

```
FUNCTION check_distance_feasibility(order, ship_params):
    
    # 大圆距离（粗略估计，实际航线可能更长）
    D_direct_nm = great_circle_distance(
        order.departure_lat, order.departure_lon,
        order.destination_lat, order.destination_lon
    ) / 1852
    
    # 航线距离通常比大圆距离长 10~30%
    D_estimated_nm = D_direct_nm × 1.2    # 保守估计
    
    # 检查续航力
    IF D_estimated_nm > ship_params.max_range_nm:
        RETURN {
            feasible: false,
            reason: f"航次距离 {D_estimated_nm:.0f}nm 超过本船续航力 {ship_params.max_range_nm:.0f}nm"
        }
    
    RETURN {feasible: true, estimated_distance_nm: D_estimated_nm}
```

### 8.2 时间可行性

```
FUNCTION check_time_feasibility(order, D_estimated, ship_params):
    
    available_time = (order.eta_latest - order.planned_departure_time).total_seconds() / 3600    # 小时
    
    # 全速航行所需最短时间
    T_min_hours = D_estimated / (ship_params.V_max_knots)
    
    IF available_time < T_min_hours × 1.1:    # 加 10% 余量
        RETURN {
            feasible: false,
            reason: f"可用时间 {available_time:.1f}h 不足以全速航行 {T_min_hours:.1f}h 到达"
        }
    
    # 经济航速所需时间
    T_econ_hours = D_estimated / (ship_params.V_econ_knots)
    
    IF available_time > T_econ_hours × 3.0:
        # 时间过于充裕——可能指令有误
        log_event("order_excessive_time", {available: available_time, econ_time: T_econ_hours})
        # 不拒绝但标记警告
    
    RETURN {feasible: true, T_min: T_min_hours, T_econ: T_econ_hours}
```

### 8.3 燃油可行性

```
FUNCTION check_fuel_feasibility(order, D_estimated, ship_params):
    
    # 估算燃油消耗（经济航速）
    fuel_rate_econ = ship_params.fuel_rate_at_econ    # kg/nm
    fuel_required = D_estimated × fuel_rate_econ × 1852    # 转换为米再乘率... 
    # 简化：fuel_required = D_nm × fuel_rate_per_nm
    fuel_required_kg = D_estimated × ship_params.fuel_rate_kg_per_nm
    
    # 加安全余量（30%）+ 应急储备
    fuel_required_total = fuel_required_kg × 1.3 + ship_params.emergency_fuel_reserve_kg
    
    IF order.fuel_on_board_kg < fuel_required_total:
        RETURN {
            feasible: false,
            reason: f"燃油不足：需要 {fuel_required_total:.0f}kg，现有 {order.fuel_on_board_kg:.0f}kg"
        }
    
    RETURN {feasible: true, fuel_required: fuel_required_kg, fuel_margin_ratio: order.fuel_on_board_kg / fuel_required_total}
```

### 8.4 吃水可行性

```
FUNCTION check_draft_feasibility(order, destination_info):
    
    max_draft = max(order.draft_fore_m, order.draft_aft_m)
    
    # 目的港最小水深（如已知）
    IF destination_info.min_depth IS NOT NULL:
        required_depth = max_draft + UKC_MIN_PORT
        IF destination_info.min_depth < required_depth:
            RETURN {
                feasible: false,
                reason: f"目的港最小水深 {destination_info.min_depth:.1f}m 不足（需要 {required_depth:.1f}m）"
            }
    
    RETURN {feasible: true}

UKC_MIN_PORT = 0.5    # 米——港内最小 UKC
```

### 8.5 综合可行性判定

```
FUNCTION precheck_feasibility(order, ship_params):
    
    results = []
    
    distance_check = check_distance_feasibility(order, ship_params)
    results.append(distance_check)
    IF NOT distance_check.feasible:
        RETURN reject_order(order, distance_check.reason)
    
    time_check = check_time_feasibility(order, distance_check.estimated_distance_nm, ship_params)
    results.append(time_check)
    IF NOT time_check.feasible:
        RETURN reject_order(order, time_check.reason)
    
    fuel_check = check_fuel_feasibility(order, distance_check.estimated_distance_nm, ship_params)
    results.append(fuel_check)
    IF NOT fuel_check.feasible:
        RETURN reject_order(order, fuel_check.reason)
    
    draft_check = check_draft_feasibility(order, get_destination_info(order.destination_port))
    results.append(draft_check)
    IF NOT draft_check.feasible:
        RETURN reject_order(order, draft_check.reason)
    
    RETURN {feasible: true, checks: results}
```

---

## 9. 步骤四：航次参数补全与默认值填充

```
FUNCTION fill_defaults(order, ship_params):
    
    IF order.optimization_priority IS NULL OR order.optimization_priority == "":
        order.optimization_priority = "fuel_optimal"
    
    IF order.planned_speed_knots == 0 OR isnan(order.planned_speed_knots):
        IF order.optimization_priority == "fuel_optimal":
            order.planned_speed_knots = ship_params.V_econ_knots
        ELIF order.optimization_priority == "time_optimal":
            order.planned_speed_knots = ship_params.V_max_knots × 0.9    # 不用全速
        ELSE:
            order.planned_speed_knots = (ship_params.V_econ_knots + ship_params.V_max_knots) / 2
    
    IF order.weather_avoidance IS NULL:
        order.weather_avoidance = "auto"
    
    IF order.report_interval_min == 0:
        order.report_interval_min = 30    # 默认 30 分钟
    
    IF order.eta_earliest IS NULL:
        # 最早到达 = 全速航行时间
        T_min = distance_check.estimated_distance_nm / ship_params.V_max_knots
        order.eta_earliest = order.planned_departure_time + timedelta(hours=T_min)
    
    # 安全吃水补全
    IF order.draft_fore_m ≤ 0:
        order.draft_fore_m = ship_params.design_draft
    IF order.draft_aft_m ≤ 0:
        order.draft_aft_m = ship_params.design_draft
    
    RETURN order
```

---

## 10. 步骤五：航次指令分解与下发

### 10.1 构建 VoyageTask

```
FUNCTION build_voyage_task(order, feasibility_results):
    
    task = VoyageTask()
    task.task_id = generate_unique_id()
    task.order_id = order.order_id
    task.status = "PLANNING"
    
    # 起终点
    task.departure = {lat: order.departure_lat, lon: order.departure_lon, port: order.departure_port}
    task.destination = {lat: order.destination_lat, lon: order.destination_lon, port: order.destination_port}
    
    # 时间窗口
    task.departure_time = order.planned_departure_time
    task.eta_window = {earliest: order.eta_earliest, latest: order.eta_latest}
    
    # 航行参数
    task.planned_speed_knots = order.planned_speed_knots
    task.optimization_priority = order.optimization_priority
    task.weather_avoidance = order.weather_avoidance
    
    # 本船参数（快照）
    task.ship_draft = max(order.draft_fore_m, order.draft_aft_m)
    task.fuel_available_kg = order.fuel_on_board_kg
    task.estimated_distance_nm = feasibility_results.estimated_distance_nm
    
    # 约束
    task.mandatory_waypoints = order.mandatory_waypoints
    task.exclusion_zones = order.exclusion_zones
    task.special_restrictions = order.special_restrictions
    
    # 通信
    task.report_interval_min = order.report_interval_min
    
    RETURN task
```

### 10.2 下发

```
FUNCTION dispatch_voyage_task(task):
    
    # 发布到 Mission Layer 内部话题
    task_publisher.publish(task)    # → Weather Routing 和 ETA/Fuel Optimizer 订阅
    
    # 更新航次状态
    voyage_status[task.task_id] = "PLANNING"
    
    # 通知岸基
    report_to_shore("voyage_order_accepted", {
        order_id: task.order_id,
        task_id: task.task_id,
        status: "PLANNING",
        estimated_distance: task.estimated_distance_nm
    })
    
    log_event("voyage_task_dispatched", task)
```

---

## 11. 航次指令格式定义

### 11.1 JSON 格式（岸基通信用）

```json
{
    "order_id": "VO-2026-04-26-001",
    "issuer": "SANGO-SHORE-OPS",
    "issue_time": "2026-04-26T08:00:00Z",
    "auth_token": "eyJhbGciOiJIUzI1NiIs...",
    "order_type": "new",
    
    "departure": {
        "port": "CNSHA",
        "latitude": 31.230,
        "longitude": 121.474,
        "time": "2026-04-26T10:00:00Z"
    },
    
    "destination": {
        "port": "CNNGB",
        "latitude": 29.868,
        "longitude": 121.544,
        "eta_earliest": "2026-04-26T20:00:00Z",
        "eta_latest": "2026-04-27T06:00:00Z"
    },
    
    "navigation": {
        "optimization": "fuel_optimal",
        "planned_speed_knots": 0,
        "weather_avoidance": "auto",
        "mandatory_waypoints": [],
        "exclusion_zones": []
    },
    
    "vessel": {
        "draft_fore_m": 0.8,
        "draft_aft_m": 0.85,
        "fuel_on_board_kg": 500,
        "restrictions": []
    },
    
    "communication": {
        "report_interval_min": 30,
        "emergency_contact": "+86-21-XXXXXXXX"
    }
}
```

### 11.2 YAML 格式（本地配置用）

对于开发和测试阶段，支持 YAML 格式的航次指令文件：

```yaml
order_id: "VO-TEST-001"
departure:
  port: "CNSHA"
  lat: 31.230
  lon: 121.474
  time: "2026-04-26T10:00:00Z"
destination:
  port: "CNNGB"
  lat: 29.868
  lon: 121.544
  eta_latest: "2026-04-27T06:00:00Z"
navigation:
  optimization: fuel_optimal
  speed_knots: 0
vessel:
  draft: 0.8
  fuel_kg: 500
```

---

## 12. 航次修改与取消

### 12.1 航次修改

```
FUNCTION handle_order_modification(modification):
    
    IF modification.order_id NOT IN active_orders:
        RETURN {accepted: false, reason: "指定的航次不存在或已完成"}
    
    # 允许修改的字段
    modifiable_fields = [
        "eta_latest",           # 延长或缩短时间窗口
        "planned_speed_knots",  # 修改计划航速
        "optimization_priority", # 切换优化策略
        "destination",          # 修改目的港（较大变更）
        "mandatory_waypoints",  # 添加/删除必经点
        "exclusion_zones"       # 添加避开区域
    ]
    
    FOR EACH field IN modification.changed_fields:
        IF field NOT IN modifiable_fields:
            RETURN {accepted: false, reason: f"字段 {field} 不允许修改"}
    
    # 应用修改
    updated_order = apply_modifications(active_orders[modification.order_id], modification)
    
    # 重新做可行性预检
    feasibility = precheck_feasibility(updated_order, ship_params)
    IF NOT feasibility.feasible:
        RETURN {accepted: false, reason: feasibility.reason}
    
    # 通知 L2 重新规划
    task = build_voyage_task(updated_order, feasibility)
    task.status = "REPLANNING"
    dispatch_voyage_task(task)
    
    log_event("order_modified", {order_id: modification.order_id, changes: modification.changed_fields})
    
    RETURN {accepted: true}
```

### 12.2 航次取消

```
FUNCTION handle_order_cancellation(cancellation):
    
    IF cancellation.order_id NOT IN active_orders:
        RETURN {accepted: false, reason: "指定的航次不存在"}
    
    # 通知所有下游模块停止当前航次
    publish_cancellation(cancellation.order_id)
    
    # 更新状态
    voyage_status[cancellation.order_id] = "CANCELLED"
    
    # 船舶执行动作取决于当前状态
    IF current_navigation_state == "UNDERWAY":
        # 正在航行中取消——减速至最低速度，等待新指令
        publish_safe_speed_advisory()
        log_event("voyage_cancelled_underway", {order_id: cancellation.order_id})
    ELSE:
        log_event("voyage_cancelled_before_departure", {order_id: cancellation.order_id})
    
    report_to_shore("voyage_cancelled", {order_id: cancellation.order_id})
    
    RETURN {accepted: true}
```

---

## 13. 多航次队列管理

```
FUNCTION manage_voyage_queue(new_order, queue):
    
    # MASS_ADAS 当前设计为单航次系统——同时只执行一个航次
    # 但支持航次队列——当前航次完成后自动执行下一个
    
    IF len(queue) >= MAX_QUEUE_SIZE:
        RETURN {accepted: false, reason: f"航次队列已满（最大 {MAX_QUEUE_SIZE}）"}
    
    IF current_active_voyage IS NOT NULL AND new_order.order_type == "new":
        # 将新航次加入队列
        queue.append(new_order)
        log_event("order_queued", {order_id: new_order.order_id, queue_position: len(queue)})
        RETURN {accepted: true, status: "QUEUED", position: len(queue)}
    
    RETURN {accepted: true, status: "ACTIVE"}

MAX_QUEUE_SIZE = 5
```

---

## 14. 岸基远程指令的认证与安全

### 14.0 DMZ 网关路径（v2.0 新增）

所有岸基指令必须经过 DMZ 网关才能进入 OT 网络。Voyage Order 模块位于 OT 网络内部，不直接接触外部通信链路。完整的指令传递路径为：

```
岸基调度系统 →(TLS 1.3)→ 卫星/4G →(IT Zone)→ DMZ Gateway →(OT Zone)→ L1 Voyage Order

DMZ Gateway 执行的验证（在指令到达 Voyage Order 之前）：
1. TLS 终端解密
2. 数字签名验证（RSA/ECDSA，用岸基公钥）
3. 时间戳验证（消息年龄 < 300 秒，防重放攻击）
4. 消息类型白名单检查（仅 voyage_order/modification/cancellation/heartbeat）
5. 速率限制（< 30 条/分钟）

通过 DMZ Gateway 的指令才会被转发到 OT 网络中的 Voyage Order 模块。
Voyage Order 模块收到的指令已经过 DMZ 的一级验证，但仍需执行二级验证（auth_token HMAC）。
这形成了"双重认证"——即使 DMZ 被突破，Voyage Order 的 HMAC 验证仍能拦截伪造指令。

DMZ 网关的详细设计见《MASS_ADAS 网络安全架构技术设计文档》第 4 节。
```

### 14.1 认证机制（二级验证）

```
FUNCTION verify_auth_token(token, issuer):
    
    # 使用 HMAC-SHA256 验证令牌
    expected_token = hmac_sha256(
        key = shared_secret_keys[issuer],
        message = order_payload_hash
    )
    
    IF token == expected_token:
        RETURN true
    
    # 令牌无效——可能是伪造指令
    log_event("auth_verification_failed", {issuer, token_prefix: token[:8]})
    alert_shore("SECURITY: 收到未认证的航次指令")
    
    RETURN false
```

### 14.2 指令完整性保护

```
# 使用 JSON Web Signature (JWS) 确保指令在传输过程中未被篡改
# 岸基签名，船端验签

FUNCTION verify_message_integrity(message, signature, issuer_public_key):
    RETURN rsa_verify(message, signature, issuer_public_key)
```

### 14.3 紧急覆盖权限

```
# 紧急情况下（如碰撞风险、设备故障），船端自主系统有权拒绝或覆盖岸基指令
# 但必须记录原因并立即通知岸基

IF emergency_condition AND order_conflicts_with_safety:
    reject_order(order, "SAFETY OVERRIDE: 紧急情况下拒绝执行")
    alert_shore("SAFETY OVERRIDE", {reason, current_situation})
```

---

## 15. 航次执行状态监控与报告

### 15.1 航次状态机

```
NEW → VALIDATING → PLANNING → READY → EXECUTING → APPROACHING → COMPLETED
                                  ↓                     ↓
                              REJECTED              ABORTED
                                                      ↓
                                                  CANCELLED
```

### 15.2 定期进度报告

```
FUNCTION generate_progress_report(task, own_ship):
    
    report = VoyageProgressReport()
    report.task_id = task.task_id
    report.timestamp = now()
    
    # 当前位置
    report.current_position = own_ship.position
    report.current_speed = own_ship.sog
    report.current_heading = own_ship.heading
    
    # 进度
    D_total = task.estimated_distance_nm
    D_remaining = geo_distance(own_ship.position, task.destination) / 1852
    report.progress_ratio = 1.0 - (D_remaining / D_total)
    report.distance_remaining_nm = D_remaining
    
    # ETA 估算
    IF own_ship.sog > 0.5:
        T_remaining = D_remaining / (own_ship.sog / 0.5144)    # 节
        report.eta_current = now() + timedelta(hours=T_remaining)
    ELSE:
        report.eta_current = NULL
    
    # 燃油
    report.fuel_remaining_kg = fuel_monitor.current_fuel
    report.fuel_consumed_kg = task.fuel_available_kg - report.fuel_remaining_kg
    
    # 状态
    report.status = voyage_status[task.task_id]
    report.alerts = get_active_alerts()
    
    RETURN report
```

### 15.3 报告发送

```
FUNCTION send_progress_report(report):
    
    # 定期发送
    IF now() - last_report_time > task.report_interval_min × 60:
        shore_link.publish(report)
        last_report_time = now()
    
    # 状态变化时立即发送
    IF status_changed:
        shore_link.publish_urgent(report)
    
    # ETA 偏差过大时发送
    IF abs(report.eta_current - task.eta_latest) < timedelta(hours=1):
        shore_link.publish_urgent(report)    # 接近截止时间
```

---

## 16. 紧急航次变更处理

```
FUNCTION handle_emergency_change(emergency_type, details):
    
    SWITCH emergency_type:
        
        CASE "weather_deterioration":
            # 气象恶化——可能需要避风港
            request_weather_routing_update()
            IF weather_routing.recommends_diversion:
                modify_destination(weather_routing.safe_harbor)
        
        CASE "equipment_failure":
            # 设备故障——可能需要降速或返港
            IF details.affects_propulsion:
                modify_speed(reduced_max_speed)
            IF details.severity == "CRITICAL":
                initiate_return_to_port()
        
        CASE "medical_emergency":
            # 医疗紧急——可能需要前往最近港口
            nearest_port = find_nearest_port_with_medical()
            modify_destination(nearest_port)
        
        CASE "security_threat":
            # 安全威胁——执行安全规程
            activate_security_protocol()
    
    # 通知岸基
    alert_shore("EMERGENCY_VOYAGE_CHANGE", {type: emergency_type, details})
```

---

## 17. 内部子模块分解

| 子模块 | 职责 | 建议语言 |
|-------|------|---------|
| Order Receiver | 指令接收、解析、认证 | C++/Python |
| Completeness Validator | 必填字段和逻辑一致性检查 | C++ |
| Feasibility Checker | 距离/时间/燃油/吃水预检 | C++ |
| Default Filler | 参数补全 | C++ |
| Task Builder | VoyageTask 构建和分发 | C++ |
| Modification Handler | 航次修改和取消处理 | C++ |
| Queue Manager | 多航次队列管理 | C++ |
| Progress Monitor | 执行进度监控和报告生成 | Python |
| Security Module | 认证、签名验证、权限管理 | C++ |

---

## 18. 数值算例

### 算例：上海 → 宁波航次

```
出发港：上海 (31.230°N, 121.474°E)
目的港：宁波 (29.868°N, 121.544°E)
出发时间：2026-04-26 10:00 UTC
最晚到达：2026-04-27 06:00 UTC（20 小时窗口）

大圆距离 = haversine(31.23, 121.47, 29.87, 121.54) ≈ 152 km ≈ 82 nm
估计航线距离 = 82 × 1.2 = 98 nm

全速时间（V_max = 19.4 节）: 98/19.4 = 5.1 小时 → 可行（窗口 20 小时 >> 5.1 小时）
经济航速时间（V_econ = 13.6 节）: 98/13.6 = 7.2 小时 → 可行

燃油估算（假设 5 kg/nm）: 98 × 5 = 490 kg
需要燃油 = 490 × 1.3 + 50（储备）= 687 kg
现有 500 kg < 687 kg → ⚠️ 燃油可能不足！

→ 系统反馈："建议在出发前补给至少 200kg 燃油，或切换到经济航速优化"
```

---

## 19. 参数总览表

| 参数 | 默认值 | 说明 |
|------|-------|------|
| 最大航次距离 | 500 nm | |
| 最短航次时间余量 | ×1.1 | 全速时间 +10% |
| 燃油安全余量 | ×1.3 | 30% 余量 |
| 应急燃油储备 | 50 kg | |
| 港内最小 UKC | 0.5 m | |
| 指令有效期 | 24 小时 | |
| 最大队列长度 | 5 | |
| 默认报告间隔 | 30 分钟 | |
| 默认优化策略 | fuel_optimal | |
| 默认气象规避 | auto | |

---

## 20. 与其他模块的协作关系

| 交互方 | 方向 | 数据 |
|-------|------|------|
| 岸基调度 → Voyage Order | 输入 | VoyageOrderMessage |
| Voyage Order → Weather Routing | 输出 | VoyageTask |
| Voyage Order → ETA/Fuel Optimizer | 输出 | VoyageTask |
| Voyage Order → Voyage Planner (L2) | 间接 | 通过 Weather Routing 传递优化后的航线参数 |
| Voyage Order → Shore Link | 输出 | 状态报告、进度报告 |
| Nav Filter → Voyage Order | 输入 | 本船位置和状态（进度计算） |
| Parameter DB → Voyage Order | 输入 | 船舶参数（可行性计算） |

---

## 21. 测试策略与验收标准

| 场景 | 描述 | 验收标准 |
|------|------|---------|
| VOR-001 | 正常航次指令接收 | 解析正确 + 校验通过 + 下发成功 |
| VOR-002 | 缺少必填字段 | 拒绝 + 明确指出缺少的字段 |
| VOR-003 | 距离超出续航力 | 拒绝 + 说明原因 |
| VOR-004 | 时间不足 | 拒绝 + 计算最短所需时间 |
| VOR-005 | 燃油不足 | 拒绝 + 建议补给量 |
| VOR-006 | 吃水超出目的港限制 | 拒绝 + 说明原因 |
| VOR-007 | 认证失败 | 拒绝 + 安全告警 |
| VOR-008 | 航次修改 | 正确应用修改 + 重新规划 |
| VOR-009 | 航次取消 | 正确停止 + 通知岸基 |
| VOR-010 | 进度报告按时发送 | 间隔 ≤ 设定值 × 1.1 |
| VOR-011 | ETA 偏差告警 | 偏差 > 1 小时时发送 |
| VOR-012 | 紧急航次变更 | 正确响应各类紧急情况 |
| VOR-013 | 多航次队列 | 队列管理正确 |
| VOR-014 | YAML 格式指令解析 | 正确解析 + 校验 |
| VOR-015 | 指令超时过期 | 24 小时未执行自动过期 |

---

## 22. 参考文献与标准

| 编号 | 文献/标准 | 相关内容 |
|------|---------|---------|
| [1] | IMO Resolution A.1106(29) "Guidelines for Voyage Planning" | 航次计划指南 |
| [2] | ISM Code (International Safety Management) | 安全管理体系（航次指令记录要求） |
| [3] | IMO MSC.1/Circ.1638 | MASS 监管框架 |
| [4] | BIMCO "Voyage Chartering" | 航次租约标准条款 |
| [5] | UN/LOCODE | 联合国港口代码标准 |

---

**文档结束**
