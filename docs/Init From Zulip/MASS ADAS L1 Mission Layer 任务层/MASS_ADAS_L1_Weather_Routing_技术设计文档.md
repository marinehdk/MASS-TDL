# MASS_ADAS Weather Routing 气象航路模块技术设计文档

**文档编号：** SANGO-ADAS-WTR-001  
**版本：** 0.1 草案  
**日期：** 2026-04-26  
**编制：** 系统架构组  

---

## 目录

1. 概述与职责定义
2. 输入与输出接口
3. 核心参数数据库
4. 船长的气象航路选择思维模型
5. 计算流程总览
6. 步骤一：气象数据获取与解析
7. 步骤二：气象格点场构建
8. 步骤三：航路搜索算法
9. 步骤四：航路安全评估
10. 步骤五：航路优化与推荐
11. 气象数据源
12. 风浪对船舶性能的影响模型
13. 台风/强风暴规避
14. 航路动态更新
15. 内部子模块分解
16. 数值算例
17. 参数总览表
18. 与其他模块的协作关系
19. 测试策略与验收标准
20. 参考文献

---

## 1. 概述与职责定义

### 1.1 模块定位

Weather Routing（气象航路）是 Layer 1（Mission Layer）中负责根据气象预报数据优化航线的子模块。它接收 Voyage Order 下发的航次任务，结合气象预报（风场、浪场、海流场），搜索一条在安全和效率之间最优的航路，传递给 ETA/Fuel Optimizer 做速度优化后下发给 L2 Voyage Planner。

对于 SANGO 的 12m USV，气象航路的意义在于：小型船舶对恶劣海况非常敏感——3 米有义波高对万吨轮可能只是"有些颠簸"，但对 12m USV 可能意味着"危险到无法安全航行"。因此气象规避对 USV 比对大船更加重要。

### 1.2 核心职责

- **气象数据获取：** 从气象服务商获取风场、浪场、海流场的网格化预报数据。
- **航路搜索：** 使用改进 Dijkstra 或 A* 算法在气象格点场上搜索最优航路。
- **安全评估：** 评估候选航路上每个时间-空间点的气象条件是否在本船安全操纵包络之内。
- **航路优化：** 在多条可行航路中选择燃油消耗最小或时间最短的。
- **动态更新：** 航行过程中气象预报更新时，重新评估当前航路是否仍然最优。

### 1.3 V1 阶段简化

对于 SANGO 目前的运营范围（中国沿海近距离航次，通常 < 200nm），气象航路的复杂度低于跨洋航行。V1 阶段简化为：

```
1. 获取出发时的气象预报
2. 检查计划航线上是否有恶劣天气区域
3. 如果有，生成绕行建议
4. 如果没有，直接使用原航线
```

跨洋级别的完整气象航路优化（等时线法、动态规划）作为 V2 目标。

---

## 2. 输入与输出接口

### 2.1 输入

| 输入项 | 来源 | 说明 |
|-------|------|------|
| 航次任务 | Voyage Order | VoyageTask（起终点、时间窗口、优化策略） |
| 气象预报数据 | 气象服务商 API | 风场、浪场、海流场（GFS/ECMWF/中国气象局） |
| 台风/热带气旋信息 | 气象服务商 | 台风路径预测 |
| 本船性能参数 | Parameter DB | 最大允许海况、失速速度表 |

### 2.2 输出

```
WeatherRoutingResult:
    Header header
    string task_id
    
    # 推荐航路
    RouteCandidate[] candidates         # 候选航路列表（按优化指标排序）
    uint32 recommended_index            # 推荐航路的索引
    
    # 推荐航路的气象摘要
    WeatherSummary weather_summary      # 沿航路的气象预报摘要
    
    # 安全评估
    bool route_safe                     # 推荐航路是否在安全包络内
    SafetyWarning[] warnings            # 气象安全警告

RouteCandidate:
    GeoPoint[] waypoints                # 航路经过点
    float64 total_distance_nm           # 总距离
    float64 estimated_fuel_kg           # 估计燃油消耗
    float64 estimated_time_hours        # 估计航行时间
    float64 max_wave_height_m           # 沿途最大预测波高
    float64 max_wind_speed_knots        # 沿途最大预测风速
    string route_label                  # "direct"/"northern_diversion"/"southern_diversion"/...

WeatherSummary:
    float64 avg_wind_speed_knots        # 平均风速
    float64 max_wind_speed_knots        # 最大风速
    float64 avg_wave_height_m           # 平均波高
    float64 max_wave_height_m           # 最大波高
    float64 avg_current_speed_knots     # 平均海流速度
    string dominant_wind_direction      # 主导风向
    string weather_trend                # "improving"/"stable"/"deteriorating"
```

---

## 3. 核心参数数据库

### 3.1 本船气象安全限制

| 参数 | 值 | 说明 |
|------|-----|------|
| 最大允许有义波高 H_s_max | 2.5 m | 12m USV 的安全操纵上限 |
| 最大允许风速 | 35 节（18 m/s） | 蒲福风力 7~8 级 |
| 建议降速波高 | 1.5 m | 超过此波高开始失速 |
| 最大允许横浪周期 | 与固有横摇周期接近时危险 | 共振风险 |
| 台风最小避让距离 | 200 nm | 台风中心的安全距离 |
| 热带风暴最小避让距离 | 100 nm | |

### 3.2 航路搜索参数

| 参数 | 值 | 说明 |
|------|-----|------|
| 格点分辨率 | 0.25° × 0.25° | 约 25km，与 GFS 预报分辨率匹配 |
| 时间步长 | 6 小时 | 与气象预报时次匹配 |
| 搜索范围（偏离大圆的最大距离） | 100 nm | 不考虑过于绕远的航路 |
| 航路候选数量 | 3~5 | 提供给操作员/系统选择 |

### 3.3 气象预报不确定性余量

| 预报时效 | 波高不确定性余量 | 风速不确定性余量 |
|---------|----------------|----------------|
| 0~24 小时 | +0.3 m | +5 节 |
| 24~48 小时 | +0.5 m | +8 节 |
| 48~72 小时 | +0.8 m | +12 节 |
| > 72 小时 | +1.2 m | +15 节 |

---

## 4. 船长的气象航路选择思维模型

### 4.1 船长如何选择航路

船长在做气象航路选择时的思维过程：

**第一看天气预报：** 出发前他会查看未来 3~5 天的气象预报——风场图、浪场图、天气系统的移动趋势。对于沿海航行，主要关注的是：有没有冷锋过境（带来大风浪）、有没有台风或热带风暴、季风方向和强度。

**第二画大圆航线：** 他先画一条起终点之间的最短航线（大圆或恒向线），然后检查这条航线上各点各时刻的气象条件。

**第三标注危险区域：** 在航线上标注出气象条件超出安全限制的区域——比如"明天下午长江口附近预计浪高 3 米"。

**第四绕行评估：** 如果直航线穿越危险区域，他会评估绕行——往北绕还是往南绕？绕多远？绕行增加多少距离和时间？

**第五权衡决策：** "北绕多走 30 海里多花 3 小时，但浪高只有 1 米。南绕只多走 15 海里，但还是有 2 米的浪。直走浪高 3 米，太危险了。选北绕。"

### 4.2 对于沿海短途的简化

对于 SANGO 的典型航次（沿海 50~200nm），气象航路选择通常很简单：

- 大部分时候天气良好——直航即可，不需要绕行。
- 偶尔有冷锋或台风——评估是否需要推迟出发（等天气好转）或绕行。
- 极端天气——直接取消航次，等待天气改善。

V1 阶段的 Weather Routing 就是实现这个简化版的决策流程。

---

## 5. 计算流程总览

```
航次任务 + 气象预报数据
      │
      ▼
┌──────────────────────────────────┐
│ 步骤一：气象数据获取与解析       │
│ 下载 GFS/ECMWF 预报数据          │
└────────────┬─────────────────────┘
             │
             ▼
┌──────────────────────────────────┐
│ 步骤二：气象格点场构建           │
│ 在航线区域构建风/浪/流的时空场   │
└────────────┬─────────────────────┘
             │
             ▼
┌──────────────────────────────────┐
│ 步骤三：直航线气象检查           │
│ 沿直航线采样气象条件             │
│ 判断是否有超出安全限制的区域      │
└────────────┬─────────────────────┘
             │ 全部安全 → 推荐直航
             │ 有危险区 ↓
             ▼
┌──────────────────────────────────┐
│ 步骤四：绕行航路搜索             │
│ A* 或 Dijkstra 搜索可行航路      │
└────────────┬─────────────────────┘
             │
             ▼
┌──────────────────────────────────┐
│ 步骤五：航路评估与推荐           │
│ 比较各候选航路的距离/时间/气象    │
│ 推荐最优航路                     │
└────────────┬─────────────────────┘
             │
             ▼
输出：WeatherRoutingResult
```

---

## 6. 步骤一：气象数据获取与解析

### 6.1 数据源

```
# 优先级 1：中国气象局（CMA）海洋气象预报
# 适用于中国近海，分辨率高

# 优先级 2：NOAA GFS（Global Forecast System）
# 全球覆盖，0.25° 分辨率，免费
# 下载地址：https://nomads.ncep.noaa.gov/

# 优先级 3：ECMWF（欧洲中期天气预报中心）
# 全球最准确的气象模式，但需要商业授权

FUNCTION fetch_weather_data(region_bbox, forecast_hours):
    
    # 尝试按优先级获取
    FOR source IN ["CMA", "GFS", "ECMWF"]:
        data = try_fetch(source, region_bbox, forecast_hours)
        IF data IS NOT NULL:
            log_event("weather_data_fetched", {source, coverage: region_bbox})
            RETURN parse_weather_data(data, source)
    
    # 全部失败——使用本地缓存的最近一次预报
    IF cached_forecast IS NOT NULL AND cache_age < 12_hours:
        log_event("using_cached_weather", {age: cache_age})
        RETURN cached_forecast
    
    # 无气象数据——只能用气候统计默认值
    log_event("no_weather_data_available")
    RETURN default_climatology(region_bbox, current_month)
```

### 6.2 GFS GRIB2 数据解析

```
FUNCTION parse_gfs_grib2(grib_file, variables):
    
    # 使用 eccodes 或 pygrib 库解析 GRIB2 格式
    import eccodes
    
    fields = {}
    
    with open(grib_file, 'rb') as f:
        while True:
            msg = eccodes.codes_grib_new_from_file(f)
            if msg is None:
                break
            
            shortName = eccodes.codes_get(msg, 'shortName')
            
            IF shortName IN variables:
                lats = eccodes.codes_get_array(msg, 'latitudes')
                lons = eccodes.codes_get_array(msg, 'longitudes')
                values = eccodes.codes_get_array(msg, 'values')
                forecast_hour = eccodes.codes_get(msg, 'forecastTime')
                
                fields[shortName + f"_t{forecast_hour}"] = {
                    lats: lats, lons: lons, values: values
                }
            
            eccodes.codes_release(msg)
    
    RETURN fields

# 需要的变量：
# 10u, 10v: 10m 风速 U/V 分量
# swh: 有义波高
# mwd: 平均波向
# mwp: 平均波周期
```

---

## 7. 步骤二：气象格点场构建

```
FUNCTION build_weather_grid(weather_data, region_bbox, time_range):
    
    # 构建时空网格
    lat_range = np.arange(region_bbox.lat_min, region_bbox.lat_max, GRID_RESOLUTION)
    lon_range = np.arange(region_bbox.lon_min, region_bbox.lon_max, GRID_RESOLUTION)
    time_steps = np.arange(time_range.start, time_range.end, TIME_STEP)
    
    grid = WeatherGrid(lat_range, lon_range, time_steps)
    
    FOR EACH t IN time_steps:
        FOR EACH lat IN lat_range:
            FOR EACH lon IN lon_range:
                # 插值到格点
                wind_u = interpolate(weather_data.wind_u, lat, lon, t)
                wind_v = interpolate(weather_data.wind_v, lat, lon, t)
                wind_speed = sqrt(wind_u² + wind_v²)
                wind_dir = atan2(wind_u, wind_v) × 180/π
                
                wave_height = interpolate(weather_data.swh, lat, lon, t)
                wave_period = interpolate(weather_data.mwp, lat, lon, t)
                wave_dir = interpolate(weather_data.mwd, lat, lon, t)
                
                grid.set(lat, lon, t, {
                    wind_speed, wind_dir,
                    wave_height, wave_period, wave_dir
                })
    
    RETURN grid
```

---

## 8. 步骤三：航路搜索算法

### 8.1 直航线气象检查

```
FUNCTION check_direct_route_weather(departure, destination, departure_time, grid, ship_params):
    
    # 沿大圆航线采样
    n_samples = 20
    total_distance = great_circle_distance(departure, destination)
    speed = ship_params.V_econ_mps
    
    max_wave = 0
    max_wind = 0
    dangerous_segments = []
    
    FOR i = 0 TO n_samples:
        fraction = i / n_samples
        point = interpolate_great_circle(departure, destination, fraction)
        time_at_point = departure_time + timedelta(seconds=fraction × total_distance / speed)
        
        weather = grid.get(point.lat, point.lon, time_at_point)
        
        # 加上预报不确定性余量
        forecast_age = (time_at_point - departure_time).total_hours()
        margin = get_forecast_uncertainty_margin(forecast_age)
        
        effective_wave = weather.wave_height + margin.wave_height
        effective_wind = weather.wind_speed + margin.wind_speed
        
        max_wave = max(max_wave, effective_wave)
        max_wind = max(max_wind, effective_wind)
        
        IF effective_wave > ship_params.H_s_max OR effective_wind > ship_params.wind_max:
            dangerous_segments.append({
                position: point,
                time: time_at_point,
                wave_height: effective_wave,
                wind_speed: effective_wind,
                fraction: fraction
            })
    
    IF len(dangerous_segments) == 0:
        RETURN {safe: true, max_wave, max_wind, route: "direct"}
    ELSE:
        RETURN {safe: false, dangerous_segments, max_wave, max_wind}
```

### 8.2 A* 搜索绕行航路

```
FUNCTION search_weather_route(departure, destination, departure_time, grid, ship_params):
    
    # A* 搜索在气象格点场上的最优路径
    # 节点 = (lat, lon, time)
    # 代价 = 航行时间（或燃油消耗）
    # 约束 = 气象条件在安全包络内
    
    start_node = (departure.lat, departure.lon, departure_time)
    goal = (destination.lat, destination.lon)
    
    open_set = PriorityQueue()
    open_set.put(start_node, priority=0)
    came_from = {}
    cost_so_far = {start_node: 0}
    
    WHILE NOT open_set.empty():
        current = open_set.get()
        
        IF close_to_goal(current, goal):
            # 找到路径——回溯
            path = reconstruct_path(came_from, current)
            RETURN path
        
        # 扩展相邻节点（8 个方向 + 等待选项）
        FOR EACH neighbor IN get_neighbors(current, grid):
            
            # 检查气象安全性
            weather = grid.get(neighbor.lat, neighbor.lon, neighbor.time)
            IF NOT is_weather_safe(weather, ship_params):
                CONTINUE    # 该方向不安全，跳过
            
            # 计算移动代价（考虑风浪对速度的影响）
            leg_distance = geo_distance(current, neighbor)
            effective_speed = compute_speed_in_weather(
                ship_params.V_econ, weather, heading_to_neighbor
            )
            leg_time = leg_distance / effective_speed
            
            new_cost = cost_so_far[current] + leg_time
            
            IF neighbor NOT IN cost_so_far OR new_cost < cost_so_far[neighbor]:
                cost_so_far[neighbor] = new_cost
                priority = new_cost + heuristic(neighbor, goal, ship_params)
                open_set.put(neighbor, priority)
                came_from[neighbor] = current
    
    # 没找到路径——所有方向都不安全
    RETURN NULL

FUNCTION heuristic(node, goal, ship_params):
    # 大圆距离 / 最大速度 = 最优估计时间
    RETURN great_circle_distance(node, goal) / ship_params.V_max_mps
```

### 8.3 气象安全性判定

```
FUNCTION is_weather_safe(weather, ship_params):
    
    IF weather.wave_height > ship_params.H_s_max:
        RETURN false
    
    IF weather.wind_speed > ship_params.wind_max:
        RETURN false
    
    # 共振风险检查：如果波浪周期接近船舶固有横摇周期
    IF abs(weather.wave_period - ship_params.roll_natural_period) < 1.0:
        # 横浪共振风险——在该方向上不安全
        RETURN false
    
    RETURN true
```

---

## 9. 步骤四：航路安全评估

### 9.1 失速速度模型

在逆风逆浪中船速会降低（失速效应）：

```
FUNCTION compute_speed_in_weather(V_calm, weather, heading):
    
    # 相对波向
    relative_wave_dir = normalize_pm180(weather.wave_dir - heading)
    
    # 失速系数（Kwon's method 简化）
    IF abs(relative_wave_dir) < 30:    # 迎浪
        speed_loss_ratio = 0.02 × weather.wave_height²    # 每 1m 波高降速约 2%
    ELIF abs(relative_wave_dir) < 60:  # 斜迎浪
        speed_loss_ratio = 0.015 × weather.wave_height²
    ELIF abs(relative_wave_dir) < 120: # 横浪
        speed_loss_ratio = 0.01 × weather.wave_height²
    ELSE:                               # 顺浪
        speed_loss_ratio = 0.005 × weather.wave_height²    # 顺浪影响最小
    
    # 风阻附加失速
    relative_wind = weather.wind_speed × cos(weather.wind_dir - heading)
    IF relative_wind > 0:    # 逆风
        wind_loss_ratio = 0.001 × relative_wind²
    ELSE:
        wind_loss_ratio = 0
    
    V_effective = V_calm × (1 - speed_loss_ratio - wind_loss_ratio)
    V_effective = max(V_effective, V_calm × 0.3)    # 最低不低于 30% 原速度
    
    RETURN V_effective
```

---

## 10. 步骤五：航路优化与推荐

```
FUNCTION recommend_route(candidates, optimization_priority):
    
    # 对每条候选航路评分
    FOR EACH route IN candidates:
        
        IF optimization_priority == "fuel_optimal":
            score = -route.estimated_fuel_kg    # 燃油越少越好（取负号）
        ELIF optimization_priority == "time_optimal":
            score = -route.estimated_time_hours
        ELSE:    # balanced
            score = -(0.5 × normalize(route.fuel) + 0.5 × normalize(route.time))
        
        # 安全惩罚
        IF route.max_wave_height_m > ship_params.H_s_max × 0.8:
            score -= 100    # 接近安全极限，大幅惩罚
        
        route.score = score
    
    candidates.sort(key=lambda r: r.score, reverse=true)
    
    RETURN {candidates, recommended_index: 0}
```

---

## 11. 台风/强风暴规避

```
FUNCTION check_typhoon_avoidance(route, typhoon_forecasts, ship_params):
    
    FOR EACH typhoon IN typhoon_forecasts:
        FOR EACH point IN route.waypoints:
            # 估算船到达该点时台风的预测位置
            eta_at_point = estimate_eta(route, point)
            typhoon_pos_at_eta = interpolate_typhoon_track(typhoon, eta_at_point)
            
            distance = geo_distance(point, typhoon_pos_at_eta)
            
            IF distance < ship_params.typhoon_safe_distance:
                RETURN {
                    safe: false,
                    typhoon: typhoon.name,
                    closest_point: point,
                    distance_nm: distance / 1852,
                    recommendation: "绕行或推迟出发"
                }
    
    RETURN {safe: true}
```

---

## 12. 航路动态更新

```
FUNCTION check_route_update_needed(current_route, new_weather_data):
    
    # 每 6 小时（新预报发布时）重新评估
    weather_on_route = evaluate_weather_along_route(current_route, new_weather_data)
    
    IF weather_on_route.max_wave > ship_params.H_s_max:
        # 原航路上出现新的危险天气
        log_event("route_weather_deterioration", weather_on_route)
        
        # 重新搜索航路
        new_result = search_weather_route(current_position, destination, now(), new_weather_data, ship_params)
        
        IF new_result IS NOT NULL:
            RETURN {update_needed: true, new_route: new_result}
        ELSE:
            RETURN {update_needed: true, recommendation: "考虑就近避风"}
    
    RETURN {update_needed: false}
```

---

## 13. 内部子模块分解

| 子模块 | 职责 | 建议语言 |
|-------|------|---------|
| Weather Data Fetcher | 气象数据下载（GFS/ECMWF/CMA） | Python |
| GRIB Parser | GRIB2 格式解析 | Python (eccodes) |
| Weather Grid Builder | 时空格点场构建 | C++/Python |
| Route Searcher | A* 搜索算法 | C++ |
| Safety Evaluator | 气象安全性和失速评估 | C++ |
| Typhoon Checker | 台风路径规避检查 | Python |
| Route Optimizer | 多航路比较和推荐 | C++ |
| Dynamic Updater | 航路动态更新检查 | Python |

---

## 14. 数值算例

```
航次：上海 → 宁波，98 nm
出发时间：2026-04-26 10:00 UTC

气象预报：
  长江口区域（31°N, 122°E）：
    t+0h:  风 NE 15kn, 浪高 1.0m
    t+6h:  风 NE 20kn, 浪高 1.5m
    t+12h: 风 N 25kn, 浪高 2.0m ← 冷锋过境
    t+18h: 风 NW 30kn, 浪高 2.8m ← 超过 USV 安全限制(2.5m)
    t+24h: 风 NW 20kn, 浪高 2.0m ← 转好

直航检查：
  经济航速 13.6 节，航行时间 ≈ 7.2 小时
  到达时预测时刻 ≈ t+7h（在冷锋前通过长江口）→ 安全

  但如果推迟到 t+6h 出发：
  到达时 ≈ t+13h（冷锋过境期间）→ 浪高 2.0~2.8m ← 危险！

→ 推荐：按原计划 10:00 出发。如果推迟超过 3 小时，建议等到 t+24h（次日 10:00）再出发。
```

---

## 15. 参数总览表

| 参数 | 默认值 | 说明 |
|------|-------|------|
| 最大允许波高 | 2.5 m | 12m USV |
| 最大允许风速 | 35 节 | |
| 台风安全距离 | 200 nm | |
| 格点分辨率 | 0.25° | |
| 预报不确定性余量（24h） | +0.3m / +5kn | |
| 航路更新检查间隔 | 6 小时 | |

---

## 16. 与其他模块的协作

| 交互方 | 方向 | 数据 |
|-------|------|------|
| Voyage Order → Weather Routing | 输入 | VoyageTask |
| 气象服务 → Weather Routing | 输入 | 预报数据 |
| Weather Routing → ETA/Fuel Optimizer | 输出 | WeatherRoutingResult |
| Weather Routing → Voyage Planner (L2) | 间接 | 推荐航路的经过点 |
| Weather Routing → Shore Link | 输出 | 气象航路报告 |

---

## 17. 测试场景

| 场景 | 描述 | 验收标准 |
|------|------|---------|
| WTR-001 | 良好天气直航 | 推荐直航，无警告 |
| WTR-002 | 沿途有 3m 浪区 | 推荐绕行或推迟 |
| WTR-003 | 台风距航线 150nm | 台风警告 + 推荐规避 |
| WTR-004 | 无气象数据 | 使用气候默认值 + 警告 |
| WTR-005 | 航行中预报更新 | 正确触发动态更新 |
| WTR-006 | 全路径不安全 | 推荐推迟出发 |

---

## 参考文献

| 编号 | 文献 | 相关内容 |
|------|------|---------|
| [1] | IMO "Guidance on Weather Routing" 2008 | 气象航路指南 |
| [2] | Bowditch "American Practical Navigator" Ch.37 | 气象航路理论 |
| [3] | NOAA GFS Documentation | GFS 模式数据格式 |
| [4] | Kwon, Y.J. "Speed Loss due to Added Resistance in Wind and Waves" 2008 | 失速模型 |

---

**文档结束**
