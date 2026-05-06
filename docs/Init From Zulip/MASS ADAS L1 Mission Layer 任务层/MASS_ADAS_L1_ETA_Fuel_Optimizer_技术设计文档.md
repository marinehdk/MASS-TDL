# MASS_ADAS ETA/Fuel Optimizer ETA 与燃油优化模块技术设计文档

**文档编号：** SANGO-ADAS-EFO-001  
**版本：** 0.1 草案  
**日期：** 2026-04-26  
**编制：** 系统架构组  

---

## 目录

1. 概述与职责定义
2. 输入与输出接口
3. 核心参数数据库
4. 船东的经济决策思维模型
5. 计算流程总览
6. 步骤一：燃油消耗模型建立
7. 步骤二：ETA 窗口内的速度优化
8. 步骤三：多段速度分配
9. 步骤四：实时 ETA 更新与速度调整
10. 步骤五：燃油余量监控与预警
11. 航速-燃耗关系（Speed-Fuel Curve）
12. Just-In-Time（JIT）到港策略
13. 排放控制区（ECA）速度策略
14. 潮汐窗口约束
15. 内部子模块分解
16. 数值算例
17. 参数总览表
18. 与其他模块的协作关系
19. 测试策略与验收标准
20. 参考文献

---

## 1. 概述与职责定义

### 1.1 模块定位

ETA/Fuel Optimizer（ETA 与燃油优化器）是 Layer 1（Mission Layer）的最后一个子模块。它接收 Weather Routing 输出的推荐航路和 Voyage Order 的 ETA 窗口，计算在 ETA 窗口内使燃油消耗最小的全航程速度方案——"在不迟到的前提下，尽可能省油"。

这个模块回答的核心问题是：**在给定的时间窗口内航行这段距离，应该以什么速度走才最省油？** 答案通常不是"匀速"——因为不同航段的气象条件、水域限速、进港时间窗口各不相同，最优速度需要分段优化。

### 1.2 核心职责

- **全程速度优化：** 在 ETA 窗口约束下，计算全程各段的最优航速，使总燃油消耗最小。
- **实时 ETA 更新：** 航行过程中根据实际进度更新 ETA 预测，如果预计迟到则建议加速，如果提前则建议减速省油。
- **燃油余量监控：** 持续监控剩余燃油与预计消耗的比值，在燃油不足时预警。
- **JIT 到港：** 如果目的港有泊位/潮汐时间窗口，调整航速使到达时间恰好匹配窗口。

### 1.3 设计原则

- **经济优先原则：** 在安全和 ETA 约束范围内，始终追求燃油消耗最小化。
- **立方关系利用原则：** 船舶燃油消耗大致与速度的三次方成正比——降速 10% 可以省油约 27%。系统应充分利用这个关系。
- **弹性 ETA 原则：** 尽量在 ETA 窗口的中部到达（而非最晚），留出应对突发情况的时间裕度。

---

## 2. 输入与输出接口

### 2.1 输入

| 输入项 | 来源 | 说明 |
|-------|------|------|
| 航次任务 | Voyage Order | VoyageTask（ETA 窗口、优化策略） |
| 推荐航路 | Weather Routing | WeatherRoutingResult（航路距离、气象条件） |
| 本船性能参数 | Parameter DB | 速度-燃耗曲线、最大/最小速度 |
| 水域限速 | ENC Reader（间接） | 各航段的速度限制 |
| 当前进度 | Nav Filter | 当前位置和速度（实时 ETA 更新用） |
| 燃油存量 | 燃油传感器 | 实时燃油余量 |
| 潮汐预报（目的港） | 外部服务 | 进港潮汐窗口 |

### 2.2 输出

```
SpeedOptimizationResult:
    Header header
    string task_id
    
    # 优化后的速度方案
    SpeedPlan[] speed_plan              # 各航段的最优速度
    float64 total_fuel_optimized_kg     # 优化后的总燃油消耗
    float64 total_fuel_full_speed_kg    # 全速航行的燃油消耗（对比用）
    float64 fuel_saving_ratio           # 节油比例
    Time eta_optimized                  # 优化后的预计到达时间
    
    # 实时更新
    Time eta_current                    # 当前预计到达时间（实时更新）
    float64 speed_adjustment_knots      # 建议的速度调整量

SpeedPlan:
    uint32 leg_index                    # 航段索引
    float64 distance_nm                 # 航段距离
    float64 planned_speed_knots         # 优化后的计划速度
    float64 fuel_estimate_kg            # 该段估计燃油消耗
    float64 time_estimate_hours         # 该段估计航行时间
    string speed_reason                 # 速度选择的理由
```

---

## 3. 核心参数数据库

### 3.1 速度-燃耗曲线参数

| 参数 | 符号 | 经验初值 | 说明 |
|------|------|---------|------|
| 燃耗系数 | C_fuel | 待标定 | fuel_rate = C_fuel × V^n |
| 速度指数 | n_fuel | 3.0 | 排水型船舶典型值 2.5~3.5 |
| 参考速度燃耗率 | fuel_ref | 待标定 | 经济航速下的燃耗率（kg/nm） |
| 最低经济速度 | V_min_econ | V_max × 0.4 | 低于此速度发动机效率下降 |
| 最大速度 | V_max | 19.4 节 (10 m/s) | |
| 经济航速 | V_econ | 13.6 节 (7 m/s) | 燃油效率最优速度 |

### 3.2 ETA 优化参数

| 参数 | 值 | 说明 |
|------|-----|------|
| ETA 目标位置（在窗口中） | 窗口中部偏前 | 留 30% 时间裕度 |
| ETA 预警阈值 | ETA 超出窗口 1 小时 | 预计迟到时预警 |
| 速度调整平滑系数 | 0.1 | 建议速度不频繁大幅变化 |
| 最大单次速度调整 | ±2 节 | 每次调整不超过 2 节 |

### 3.3 燃油监控参数

| 参数 | 值 | 说明 |
|------|-----|------|
| 燃油安全余量 | 30% | 到达目的港时至少剩余 30% |
| 应急燃油储备 | 50 kg | 绝对最低储备 |
| 燃油预警阈值 | 预计到达时剩余 < 20% | |
| 燃油危急阈值 | 预计到达时剩余 < 10% | |

---

## 4. 船东的经济决策思维模型

### 4.1 速度与成本的关系

船东最关心的经济事实是：**燃油消耗与速度的立方成正比。**

```
具体来说：
  速度提高 10% → 燃油增加约 33%
  速度提高 20% → 燃油增加约 73%
  速度降低 10% → 燃油减少约 27%
  速度降低 20% → 燃油减少约 49%
```

这意味着哪怕只降速一点点，省油效果就很显著。但降速太多会导致到达时间延长——船在海上多漂一天就多一天的运营成本（船员工资、保险、折旧，虽然 USV 无船员但仍有折旧和占用成本）。

最优速度是在"省油"和"省时间"之间的平衡点。当时间充裕时，降速省油优势明显；当时间紧迫时，降速空间有限。

### 4.2 JIT（Just-In-Time）到港

现代航运中越来越流行 JIT 到港策略——不追求尽快到达，而是精确计算速度使船恰好在泊位可用时到达。这避免了"船到了但泊位还没空"的等待——锚泊等待不仅浪费燃油（锚泊时主机仍需怠速运转辅机发电），还占用锚地资源。

对于 SANGO USV，JIT 的意义在于：如果目的港的码头有时间窗口（比如只在白天接受靠泊），调整航速使到达时间匹配窗口，而不是半夜到了在港外等到天亮。

---

## 5. 计算流程总览

```
航次任务 + 推荐航路 + 本船参数
      │
      ▼
┌──────────────────────────────────┐
│ 步骤一：建立速度-燃耗模型        │
│ fuel_rate(V) = C × V^n           │
└────────────┬─────────────────────┘
             │
             ▼
┌──────────────────────────────────┐
│ 步骤二：ETA 窗口内速度优化       │
│ 求解：min fuel(V) s.t. ETA 约束  │
└────────────┬─────────────────────┘
             │
             ▼
┌──────────────────────────────────┐
│ 步骤三：多段速度分配             │
│ 考虑限速段、气象影响段            │
└────────────┬─────────────────────┘
             │
             ▼
输出：SpeedOptimizationResult
      │
      │ ← 航行中持续运行 ↓
      │
┌──────────────────────────────────┐
│ 步骤四：实时 ETA 更新与速度调整  │
│ 根据实际进度调整速度建议         │
└────────────┬─────────────────────┘
             │
┌──────────────────────────────────┐
│ 步骤五：燃油余量监控             │
│ 持续监控剩余燃油 vs 预计消耗     │
└──────────────────────────────────┘
```

---

## 6. 步骤一：燃油消耗模型建立

### 6.1 海军系数法（Admiralty Coefficient）

```
FUNCTION build_fuel_model(ship_params):
    
    # 排水型船舶的功率-速度关系：P ∝ V^n
    # 燃油消耗率 ∝ P ∝ V^n
    # 每海里的燃油消耗 = fuel_rate / V ∝ V^(n-1)
    
    # 使用参考点标定
    V_ref = ship_params.V_econ_knots    # 经济航速
    fuel_ref = ship_params.fuel_rate_at_econ_kg_per_hour    # 经济航速下的小时燃耗
    n = ship_params.fuel_speed_exponent    # 指数，典型 2.5~3.5
    
    C = fuel_ref / (V_ref ^ n)
    
    FUNCTION fuel_rate_per_hour(V_knots):
        RETURN C × V_knots ^ n    # kg/小时
    
    FUNCTION fuel_rate_per_nm(V_knots):
        RETURN C × V_knots ^ (n-1)    # kg/海里
    
    FUNCTION fuel_for_distance(V_knots, distance_nm):
        RETURN fuel_rate_per_nm(V_knots) × distance_nm    # kg
    
    FUNCTION time_for_distance(V_knots, distance_nm):
        RETURN distance_nm / V_knots    # 小时
    
    RETURN {fuel_rate_per_hour, fuel_rate_per_nm, fuel_for_distance, time_for_distance}
```

### 6.2 气象修正

```
FUNCTION fuel_rate_with_weather(V_knots, weather, heading, fuel_model):
    
    # 气象会增加阻力，导致同样速度消耗更多燃油
    # 或者说：同样的功率在恶劣天气下速度更低
    
    # 附加阻力百分比（基于 Kwon 方法）
    added_resistance_pct = compute_added_resistance(weather, heading)
    
    # 修正后的燃耗 = 无风浪燃耗 × (1 + 附加阻力%)
    fuel_corrected = fuel_model.fuel_rate_per_hour(V_knots) × (1 + added_resistance_pct / 100)
    
    RETURN fuel_corrected

FUNCTION compute_added_resistance(weather, heading):
    # 简化：附加阻力 ≈ k × H_s²（波高的平方）
    k = 3.0    # 经验系数，需标定
    RETURN k × weather.wave_height²    # 百分比
```

---

## 7. 步骤二：ETA 窗口内的速度优化

### 7.1 单段匀速优化

对于简单情况（单段航程、无中途限速），最优速度的计算是解析的：

```
FUNCTION optimize_single_leg(distance_nm, eta_window, fuel_model):
    
    T_available = (eta_window.latest - now()).total_hours()
    T_earliest = (eta_window.earliest - now()).total_hours()
    
    # 目标到达时间 = 窗口中部偏前（留 30% 裕度）
    T_target = T_earliest + (T_available - T_earliest) × 0.7
    
    # 目标速度 = 距离 / 时间
    V_target = distance_nm / T_target    # 节
    
    # 约束检查
    V_target = clamp(V_target, V_MIN_ECON, V_MAX)
    
    # 如果目标速度 > V_max：即使全速也到不了 → 警告
    IF distance_nm / V_MAX > T_available:
        log_event("eta_infeasible", {required_speed: distance_nm/T_available, max: V_MAX})
        V_target = V_MAX
        eta_warning = "预计无法在最晚到达时间内到达"
    
    # 如果目标速度 < V_min_econ：时间太充裕
    IF V_target < V_MIN_ECON:
        V_target = V_MIN_ECON
        # 会提前到达，但低于 V_min_econ 发动机效率太差不值得
    
    # 计算燃油消耗
    fuel = fuel_model.fuel_for_distance(V_target, distance_nm)
    time = fuel_model.time_for_distance(V_target, distance_nm)
    eta = now() + timedelta(hours=time)
    
    RETURN {V_target, fuel, eta, eta_warning}
```

### 7.2 对比分析

```
FUNCTION compare_speed_options(distance_nm, fuel_model):
    
    options = []
    
    FOR V IN [V_MAX, V_MAX×0.9, V_MAX×0.8, V_ECON, V_ECON×0.8, V_MIN_ECON]:
        fuel = fuel_model.fuel_for_distance(V, distance_nm)
        time = fuel_model.time_for_distance(V, distance_nm)
        
        options.append({
            speed_knots: V,
            fuel_kg: fuel,
            time_hours: time,
            fuel_vs_full_speed: fuel / fuel_model.fuel_for_distance(V_MAX, distance_nm)
        })
    
    RETURN options

# 算例（98nm，n=3）：
# V=19.4kn: time=5.1h, fuel=100%
# V=17.5kn: time=5.6h, fuel=73%  (降 10% 省 27%)
# V=15.5kn: time=6.3h, fuel=51%  (降 20% 省 49%)
# V=13.6kn: time=7.2h, fuel=34%  (降 30% 省 66%)
# V=11.0kn: time=8.9h, fuel=18%  (降 43% 省 82%)
```

---

## 8. 步骤三：多段速度分配

当航路有多段（因为中间有限速段或气象差异段）时，需要分段优化速度：

```
FUNCTION optimize_multi_leg(legs, eta_window, fuel_model):
    
    # 约束：各段时间之和 ≤ T_available
    # 目标：各段燃油之和最小
    # 各段速度约束：V_min_leg ≤ V_leg ≤ V_max_leg
    
    T_available = (eta_window.latest - now()).total_hours()
    T_target = T_available × 0.7    # 目标用 70% 的时间
    
    # 对于燃耗 ∝ V^n 的情况，最优速度分配是所有段速度相同（当无约束时）
    # 有约束时，限速段固定速度，其他段均匀分配剩余时间
    
    # 1. 先计算限速段的固定时间消耗
    T_fixed = 0
    D_fixed = 0
    fuel_fixed = 0
    
    FOR EACH leg IN legs:
        IF leg.has_speed_limit:
            V_leg = min(leg.speed_limit, V_MAX)
            T_leg = leg.distance_nm / V_leg
            fuel_leg = fuel_model.fuel_for_distance(V_leg, leg.distance_nm)
            
            T_fixed += T_leg
            D_fixed += leg.distance_nm
            fuel_fixed += fuel_leg
            leg.planned_speed = V_leg
            leg.speed_reason = f"限速 {V_leg:.1f}kn"
    
    # 2. 剩余时间分配给非限速段
    T_remaining = T_target - T_fixed
    D_remaining = sum(leg.distance for leg in legs if not leg.has_speed_limit)
    
    IF T_remaining ≤ 0:
        # 限速段已经占满时间——其他段用最大速度
        V_free = V_MAX
    ELSE:
        V_free = D_remaining / T_remaining    # 均匀速度
        V_free = clamp(V_free, V_MIN_ECON, V_MAX)
    
    # 3. 填充非限速段
    fuel_free = 0
    FOR EACH leg IN legs:
        IF NOT leg.has_speed_limit:
            # 考虑气象影响
            V_effective = min(V_free, compute_max_speed_in_weather(leg.weather, ship_params))
            leg.planned_speed = V_effective
            
            fuel_leg = fuel_model.fuel_for_distance(V_effective, leg.distance_nm)
            fuel_free += fuel_leg
            leg.speed_reason = "ETA 优化"
    
    total_fuel = fuel_fixed + fuel_free
    total_time = sum(leg.distance / leg.planned_speed for leg in legs)
    eta = now() + timedelta(hours=total_time)
    
    RETURN {legs, total_fuel, eta}
```

---

## 9. 步骤四：实时 ETA 更新与速度调整

### 9.1 实时 ETA 计算

```
FUNCTION update_eta_realtime(current_position, destination, current_speed, remaining_legs):
    
    D_remaining = compute_remaining_distance(current_position, remaining_legs)
    
    IF current_speed > 0.5:
        T_remaining = D_remaining / (current_speed / 0.5144)    # 转换为节
        eta_current = now() + timedelta(hours=T_remaining)
    ELSE:
        eta_current = NULL    # 船几乎停了
    
    RETURN eta_current
```

### 9.2 速度调整建议

```
FUNCTION compute_speed_adjustment(eta_current, eta_target, eta_window, current_speed):
    
    IF eta_current IS NULL:
        RETURN {adjustment: 0, reason: "船速过低，无法计算 ETA"}
    
    # 与目标 ETA 的偏差
    delta_hours = (eta_current - eta_target).total_hours()
    
    IF delta_hours > 1.0:
        # 预计比目标晚到 1+ 小时——建议加速
        # 但每次最多调整 2 节
        V_needed = D_remaining / (T_target_remaining)
        adjustment = min(V_needed - current_speed_knots, MAX_SPEED_ADJUSTMENT)
        adjustment = max(adjustment, 0)
        RETURN {adjustment_knots: adjustment, reason: f"预计晚到 {delta_hours:.1f}h，建议加速"}
    
    ELIF delta_hours < -2.0:
        # 预计比目标早到 2+ 小时——建议减速省油
        V_needed = D_remaining / (T_target_remaining)
        adjustment = max(V_needed - current_speed_knots, -MAX_SPEED_ADJUSTMENT)
        adjustment = min(adjustment, 0)
        RETURN {adjustment_knots: adjustment, reason: f"预计早到 {abs(delta_hours):.1f}h，建议减速省油"}
    
    ELSE:
        RETURN {adjustment_knots: 0, reason: "ETA 在目标范围内"}

MAX_SPEED_ADJUSTMENT = 2.0    # 节
```

### 9.3 ETA 超出窗口预警

```
FUNCTION check_eta_window(eta_current, eta_window):
    
    IF eta_current > eta_window.latest:
        return {
            alert: "WARNING",
            message: f"预计迟到 {(eta_current - eta_window.latest).total_hours():.1f} 小时",
            recommendation: "建议加速至最大安全速度或通知岸基调整 ETA 窗口"
        }
    
    IF eta_current < eta_window.earliest:
        RETURN {
            alert: "INFO",
            message: f"预计提前 {(eta_window.earliest - eta_current).total_hours():.1f} 小时到达",
            recommendation: "建议降速省油或在目的港外等待"
        }
    
    RETURN {alert: "NONE"}
```

---

## 10. 步骤五：燃油余量监控与预警

```
FUNCTION monitor_fuel(fuel_remaining_kg, fuel_model, remaining_distance, planned_speed):
    
    # 预计剩余消耗
    fuel_needed = fuel_model.fuel_for_distance(planned_speed, remaining_distance / 1852)
    
    # 到达时的预计余量
    fuel_at_arrival = fuel_remaining_kg - fuel_needed
    fuel_margin_ratio = fuel_at_arrival / fuel_remaining_kg
    
    IF fuel_at_arrival < EMERGENCY_FUEL_RESERVE:
        RETURN {
            alert: "CRITICAL",
            message: f"燃油严重不足：预计到达时仅剩 {fuel_at_arrival:.0f}kg < 应急储备 {EMERGENCY_FUEL_RESERVE}kg",
            recommendation: "立即降速至最低速度或前往最近港口"
        }
    
    IF fuel_margin_ratio < 0.10:
        RETURN {
            alert: "WARNING",
            message: f"燃油余量不足 10%（预计到达时剩余 {fuel_at_arrival:.0f}kg）",
            recommendation: "建议降速或缩短航次"
        }
    
    IF fuel_margin_ratio < 0.20:
        RETURN {
            alert: "CAUTION",
            message: f"燃油余量偏低（预计到达时剩余 {fuel_margin_ratio*100:.0f}%）",
            recommendation: "注意监控燃油消耗"
        }
    
    RETURN {alert: "NONE", fuel_at_arrival, fuel_margin_ratio}

EMERGENCY_FUEL_RESERVE = 50    # kg
```

---

## 11. 航速-燃耗关系（Speed-Fuel Curve）详解

```
# 排水型船舶的典型关系：

# 功率 P = k × V^n
# 其中 n ≈ 3（低速排水状态），n ≈ 4~6（高速半滑行过渡区有阻力驼峰）

# 对于 12m USV（可能有半滑行特性）：
# 低速段（V < 8 节）：n ≈ 3.0
# 中速段（8~14 节）：n ≈ 3.5（过渡区，阻力增长快于立方）
# 高速段（14~20 节）：n ≈ 2.5（可能进入滑行，阻力增长放缓）

# 分段速度指数模型：
FUNCTION fuel_rate_piecewise(V_knots, ship_params):
    
    IF V_knots < 8:
        n = 3.0
        C = ship_params.C_low
    ELIF V_knots < 14:
        n = 3.5
        C = ship_params.C_mid
    ELSE:
        n = 2.5
        C = ship_params.C_high
    
    RETURN C × V_knots ^ n    # kg/小时

# 各 C 值通过在参考速度下的已知燃耗率标定，确保分段连续
```

---

## 12. Just-In-Time（JIT）到港策略

```
FUNCTION apply_jit_strategy(eta_window, port_info):
    
    # 如果目的港有泊位时间窗口
    IF port_info.berth_window IS NOT NULL:
        berth_start = port_info.berth_window.start
        berth_end = port_info.berth_window.end
        
        # 目标到达时间 = 泊位窗口开始前 30 分钟（留引航和靠泊时间）
        target_arrival = berth_start - timedelta(minutes=30)
        
        # 如果泊位窗口在 ETA 窗口之外，以 ETA 窗口为准
        target_arrival = clamp(target_arrival, eta_window.earliest, eta_window.latest)
    
    # 如果目的港有潮汐窗口（见第 14 节）
    ELIF port_info.tide_window IS NOT NULL:
        target_arrival = port_info.tide_window.optimal
    
    ELSE:
        # 无特殊窗口——目标到达时间 = ETA 窗口的 70% 位置
        T_window = (eta_window.latest - eta_window.earliest).total_hours()
        target_arrival = eta_window.earliest + timedelta(hours=T_window × 0.7)
    
    RETURN target_arrival
```

---

## 13. 排放控制区（ECA）速度策略

对于进入 ECA（如中国沿海的船舶排放控制区）的航段：

```
FUNCTION adjust_for_eca(leg, eca_info):
    
    IF leg intersects ECA:
        # ECA 内可能需要使用低硫燃油，燃油成本更高
        # 经济上倾向于在 ECA 内降速（减少高成本燃油消耗）
        # 同时 ECA 外适当加速补偿时间
        
        leg_in_eca.planned_speed *= 0.85    # ECA 内降速 15%
        leg_in_eca.speed_reason = "ECA 区域经济降速"
        
        # 重新计算 ECA 外段的速度以维持总 ETA
```

---

## 14. 潮汐窗口约束

```
FUNCTION check_tide_window(destination_port, ship_draft):
    
    # 某些港口只能在高潮时段进入（水深限制）
    tide_predictions = get_tide_predictions(destination_port, time_range=48h)
    
    safe_windows = []
    FOR EACH tide_cycle IN tide_predictions:
        # 高潮前后的安全进港窗口
        # 水深足够的时段 = 潮高 > (船舶吃水 + UKC_min - 海图水深)
        required_tide = ship_draft + UKC_MIN_PORT - chart_depth
        
        IF required_tide ≤ 0:
            # 任何潮位都可以进港
            RETURN {constrained: false}
        
        # 找到潮高 ≥ required_tide 的时段
        window = find_time_above_threshold(tide_cycle, required_tide)
        IF window IS NOT NULL:
            safe_windows.append(window)
    
    IF len(safe_windows) == 0:
        RETURN {constrained: true, feasible: false, reason: "48 小时内无安全进港潮汐窗口"}
    
    RETURN {constrained: true, feasible: true, windows: safe_windows}
```

---

## 15. 内部子模块分解

| 子模块 | 职责 | 建议语言 |
|-------|------|---------|
| Fuel Model | 速度-燃耗模型（含气象修正） | C++ |
| Speed Optimizer | ETA 约束下的速度优化 | C++ |
| Multi-Leg Allocator | 多段速度分配 | C++ |
| ETA Tracker | 实时 ETA 更新 + 速度调整建议 | C++ |
| Fuel Monitor | 燃油余量监控 + 预警 | C++ |
| JIT Planner | JIT 到港时间计算 | Python |
| Tide Window Checker | 潮汐窗口查询 | Python |

---

## 16. 数值算例

### 算例：上海 → 宁波，98nm

```
V_max = 19.4 节, V_econ = 13.6 节
n = 3.0
fuel_ref = 8.0 kg/hr @ V_econ = 13.6 节
C = 8.0 / 13.6^3 = 0.00318

ETA 窗口：出发后 7h ~ 20h
目标到达：出发后 7 + (20-7)×0.7 = 16.1h

目标速度 = 98 / 16.1 = 6.1 节
→ 低于 V_min_econ (7.8 节)，取 V = 7.8 节
→ 时间 = 98 / 7.8 = 12.6 小时
→ ETA = 出发后 12.6h（在窗口内）

燃耗：
  fuel_rate = 0.00318 × 7.8^3 = 1.51 kg/hr
  总燃耗 = 1.51 × 12.6 = 19.0 kg

对比全速：
  fuel_rate = 0.00318 × 19.4^3 = 23.2 kg/hr
  时间 = 98/19.4 = 5.1h
  总燃耗 = 23.2 × 5.1 = 118.3 kg

节油比 = (118.3 - 19.0) / 118.3 = 84%

→ 在时间充裕时，降速至经济航速以下省油效果极其显著
```

---

## 17. 参数总览表

| 参数 | 默认值 | 说明 |
|------|-------|------|
| 燃耗速度指数 n | 3.0 | 需海试标定 |
| 最低经济速度 | V_max × 0.4 | |
| ETA 目标在窗口中的位置 | 70% | |
| 最大单次速度调整 | ±2 节 | |
| 燃油安全余量 | 30% | |
| 应急燃油储备 | 50 kg | |
| 燃油预警阈值 | 20% | |
| 速度调整平滑系数 | 0.1 | |
| ETA 预警提前量 | 1 小时 | |

---

## 18. 与其他模块的协作

| 交互方 | 方向 | 数据 |
|-------|------|------|
| Weather Routing → ETA/Fuel | 输入 | 推荐航路 + 气象条件 |
| Voyage Order → ETA/Fuel | 输入 | ETA 窗口 + 优化策略 |
| Parameter DB → ETA/Fuel | 输入 | 速度-燃耗参数 |
| ETA/Fuel → Voyage Planner (L2) | 输出 | 各段计划速度 |
| ETA/Fuel → Shore Link | 输出 | ETA 更新 + 燃油状态 |
| Nav Filter → ETA/Fuel | 输入 | 当前位置/速度（实时 ETA） |
| Fuel Sensor → ETA/Fuel | 输入 | 实时燃油余量 |

---

## 19. 测试场景

| 场景 | 描述 | 验收标准 |
|------|------|---------|
| EFO-001 | 时间充裕——降速优化 | 速度 ≤ V_econ + 燃油节省 > 30% |
| EFO-002 | 时间紧张——全速 | 速度 = V_max + ETA 可行性警告 |
| EFO-003 | ETA 不可行 | 正确拒绝 + 说明最早可达时间 |
| EFO-004 | 多段限速 | 限速段固定 + 其他段优化 |
| EFO-005 | 实时 ETA 更新——落后 | 建议加速 ≤ 2 节 |
| EFO-006 | 实时 ETA 更新——超前 | 建议降速省油 |
| EFO-007 | 燃油预警 | 余量 < 20% 时正确预警 |
| EFO-008 | 燃油危急 | 余量 < 10% 时建议就近靠泊 |
| EFO-009 | JIT 到港 | 到达时间匹配泊位窗口 ±30min |
| EFO-010 | 潮汐窗口约束 | 速度调整后匹配安全潮汐窗口 |

---

## 20. 参考文献

| 编号 | 文献 | 相关内容 |
|------|------|---------|
| [1] | IMO MEPC.203(62) EEDI | 能效设计指数 |
| [2] | IMO MEPC.282(70) "Ship Fuel Oil Consumption Data Collection" | 燃油消耗数据采集 |
| [3] | Ronen, D. "The Effect of Oil Price on the Optimal Speed of Ships" JNE, 1982 | 最优航速理论 |
| [4] | Psaraftis, H.N. "Speed Optimization vs Speed Reduction" TRE, 2019 | 航速优化综述 |
| [5] | IMO "Just In Time Arrival Guide" 2020 | JIT 到港指南 |

---

**文档结束**
