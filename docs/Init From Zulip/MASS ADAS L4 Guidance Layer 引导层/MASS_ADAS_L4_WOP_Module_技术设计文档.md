# MASS_ADAS WOP Module 转舵点模块技术设计文档

**文档编号：** SANGO-ADAS-WOP-001  
**版本：** 0.1 草案  
**日期：** 2026-04-26  
**编制：** 系统架构组  

---

## 1. 概述与职责定义

WOP Module（转舵点模块，Wheel-Over Point Module）是 Layer 4（Guidance Layer）中负责转弯时机控制的子模块。核心问题：船应该在距离转弯航点多远的位置开始打舵，使得旋回弧线精确切入下一航段方向。

打舵太晚船会冲过航线外侧（under-shoot），打舵太早会切入内侧（over-shoot）。WOP Module 找到"刚刚好"的时机。

核心职责：WOP 距离实时计算、WOP 触发判定、混合引导信号生成、转弯进度跟踪、转弯完成判定。

---

## 2. 输入与输出接口

**输入：** 距下一航点距离（LOS Guidance, 10Hz）、航段方位角 α_k 和 α_{k+1}、转角 Δψ、WOP 规划距离（WP Generator）、本船速度/航向/转向角速度（Nav Filter, 50Hz）、旋回参数（Parameter DB）。

**输出：**

```
WopOutput:
    float64 wop_distance_current        # 当前速度下的实时 WOP 距离（米）
    float64 distance_to_wop             # 距 WOP 的剩余距离（米，负=已过）
    bool wop_triggered                  # 是否已触发
    float64 blended_heading             # 混合后的目标航向（弧度）
    float64 blend_factor                # 混合因子（1=全当前段，0=全下一段）
    float64 turn_progress               # 转弯完成比例 [0,1]
    float64 heading_remaining           # 剩余航向变化量（度）
    bool turn_complete                  # 转弯是否完成
    string turn_phase                   # "approaching"/"turning"/"stabilizing"/"complete"
```

---

## 3. 核心参数

| 参数 | 符号 | 默认值 | 说明 |
|------|------|-------|------|
| WOP 安全提前系数 | k_wop_margin | 1.2 | WOP 距离 × 1.2 |
| 舵响应延迟 | t_rudder_delay | 2.0 s | 从下令到舵开始动 |
| 舵角建立时间 | t_rudder_build | 7.0 s | 从 0° 到目标舵角 |
| 船体响应延迟 | t_hull_delay | 3.0 s | 从舵生效到船偏转 |
| 惯性提前系数 | k_inertia | 1.5 | WOP_inertia = k × L |
| 小转角阈值 | — | 10° | 小于此不触发 WOP |
| 转弯完成航向阈值 | — | 5° | |
| 转弯完成角速度阈值 | — | 1°/s | |
| 稳定持续时间 | — | 5 s | |
| 后延距离 | — | min(R×0.5, 50m) | 过航点后的混合延续 |

---

## 4. 船长的转舵时机判断

船长在接近转弯航点时的三层判断：

**粗判（航前）：** 审图时根据船速和转角大小估算 WOP——对应 WP Generator 的 wop_distance。

**精判（接近时）：** 根据实际速度（可能与计划不同）、当前 CTE、风流来微调。如果速度比计划的高，WOP 要更提前；如果已经偏向转弯外侧，也需要更提前。

**体感（执行中）：** 打舵后感受转向速率是否合适——太慢则加大舵角，太快则提前回舵。

---

## 5. WOP 距离实时计算

### 5.1 基本 WOP 距离

基于当前实际速度（每 100ms 更新）：

```
FUNCTION compute_wop_distance(V, turn_angle, ship_params):
    
    Δψ = abs(turn_angle) × π / 180    # 弧度
    R = lookup_turning_radius(V, ship_params.turning_table)
    A_d = lookup_advance(V, ship_params.turning_table)
    
    # 几何 WOP
    IF Δψ <= π/2:    # ≤ 90°
        WOP_base = R × tan(Δψ / 2)
    ELSE:
        WOP_base = A_d × (Δψ / (π/2)) - R × sin(Δψ)
    
    # 延迟修正
    t_total = t_RUDDER_DELAY + t_RUDDER_BUILD/2 + t_HULL_DELAY
    WOP_delay = V × t_total
    
    # 惯性修正
    WOP_inertia = k_INERTIA × ship_params.length
    
    # 总计含安全系数
    WOP_total = (WOP_base + WOP_delay + WOP_inertia) × k_WOP_MARGIN
    
    RETURN WOP_total
```

### 5.2 CTE 修正

```
FUNCTION adjust_wop_for_cte(WOP, cte, turn_direction):
    
    # 右转且右偏→更早打舵（WOP 增大）
    IF turn_direction == "starboard" AND cte > 0:
        WOP += abs(cte) × 0.5
    # 右转且左偏→可稍晚（WOP 减小，但不低于基线的 50%）
    ELIF turn_direction == "starboard" AND cte < 0:
        WOP -= min(abs(cte) × 0.3, WOP × 0.5)
    # 左转同理对称处理
    ELIF turn_direction == "port" AND cte < 0:
        WOP += abs(cte) × 0.5
    ELIF turn_direction == "port" AND cte > 0:
        WOP -= min(abs(cte) × 0.3, WOP × 0.5)
    
    RETURN max(WOP, WOP_base × 0.5)    # 不低于基线的 50%
```

---

## 6. WOP 触发判定

```
FUNCTION check_wop_trigger(distance_to_next_wp, wop_distance):
    
    distance_to_wop = distance_to_next_wp - wop_distance
    
    IF distance_to_wop <= 0 AND NOT already_triggered:
        wop_triggered = true
        turn_start_time = now()
        turn_start_heading = own_heading
        turn_phase = "turning"
        
        log_event("wop_triggered", {
            distance_to_wp: distance_to_next_wp,
            wop_distance: wop_distance,
            speed: V, heading: own_heading
        })
    
    RETURN {wop_triggered, distance_to_wop}
```

---

## 7. 混合引导信号生成

### 7.1 余弦平滑混合

```
FUNCTION compute_blend(distance_to_wp, wop_distance, post_wp_distance):
    
    IF distance_to_wp > wop_distance:
        RETURN 1.0    # 100% 当前航段
    
    blend_zone = wop_distance + post_wp_distance
    d_into_blend = wop_distance - distance_to_wp
    
    IF d_into_blend > blend_zone:
        RETURN 0.0    # 100% 下一航段
    
    ratio = d_into_blend / blend_zone
    blend_factor = 0.5 × (1 + cos(π × ratio))    # 1→0 余弦曲线
    
    RETURN blend_factor
```

### 7.2 混合航向

```
FUNCTION compute_blended_heading(α_k, α_k1, blend_factor):
    diff = normalize_pm_pi(α_k1 - α_k)
    blended = normalize_0_2pi(α_k + (1 - blend_factor) × diff)
    RETURN blended
```

**为什么用余弦混合：** 线性混合在开始和结束时航向变化率有"折角"（阶跃），导致舵角指令不平滑。余弦混合的变化率在两端为零（正弦起始），中间最快，产生最平滑的舵角指令。

---

## 8. 转弯进度跟踪

```
FUNCTION track_turn_progress(own_heading, start_heading, target_heading, total_angle):
    
    changed = abs(normalize_pm180(own_heading - start_heading))
    progress = clamp(changed / abs(total_angle), 0, 1)
    remaining = abs(normalize_pm180(target_heading - own_heading))
    
    RETURN {progress, remaining}
```

---

## 9. 转弯完成判定

```
FUNCTION check_turn_complete(heading_error, yaw_rate):
    
    IF heading_error < 5.0 AND abs(yaw_rate) < 1.0:    # 度和度/秒
        IF turn_phase != "stabilizing":
            turn_phase = "stabilizing"
            stabilize_start = now()
        
        IF now() - stabilize_start > 5.0:    # 持续 5 秒
            turn_phase = "complete"
            RETURN true
    ELSE:
        turn_phase = "turning"
    
    RETURN false
```

---

## 10. 特殊情况处理

### 10.1 小转角（< 10°）
不触发 WOP，让 LOS 的自然切换处理。LOS 的前视距离足以平滑小转角。

### 10.2 速度变化中
每个周期用当前实际速度重新计算 WOP 距离——不使用规划时的固定值。

### 10.3 避碰中断
Tactical 层发出避碰指令时，立即中止当前转弯混合，切换到避碰航线。

### 10.4 减速中的 WOP
如果 Look-ahead Speed 正在减速，船速在持续下降。WOP 距离会随速度减小——但已经触发的 WOP 不取消（不会"取消打舵"）。

---

## 11. 数值算例

### 90° 转弯，V=8m/s

```
R(8)=25.5m, A_d(8)=48m, L=12m

WOP_base = 25.5 × tan(45°) = 25.5m
WOP_delay = 8 × (2.0 + 3.5 + 3.0) = 8 × 8.5 = 68m
WOP_inertia = 1.5 × 12 = 18m

WOP_total = (25.5 + 68 + 18) × 1.2 = 134m

→ 距航点 134m 时触发 WOP
→ 混合区域 = 134m + 13m(后延) = 147m
→ 在 147m 区域内航向从 α_k 余弦平滑过渡到 α_{k+1}
```

### CTE 修正示例

```
右转 90°，当前 CTE = +15m（右偏）
→ 右转且右偏：WOP 增大
  WOP_adjusted = 134 + 15 × 0.5 = 141.5m
→ 提前 7.5m 打舵，补偿已有的外偏
```

---

## 12. 与其他模块的协作

| 交互方 | 方向 | 数据 |
|-------|------|------|
| LOS Guidance → WOP | 输入 | distance_to_next_wp |
| WOP → LOS Guidance | 输出 | wop_triggered, blended_heading |
| Look-ahead Speed → WOP | 影响 | 减速使速度变化 → WOP 实时更新 |
| CTE Calculator → WOP | 输入 | cte_filtered（CTE 修正） |
| Nav Filter → WOP | 输入 | 航向、角速度（转弯跟踪） |
| Tactical Layer → WOP | 中断 | 避碰指令 → 中止转弯 |
| Heading Anticipation → WOP | 消费 | blended_heading → 预偏处理 |

---

## 13. 测试场景与验收标准

| 场景 | 描述 | 验收标准 |
|------|------|---------|
| WOP-001 | 30° 转弯，8 m/s | 转弯后 CTE < 10m |
| WOP-002 | 90° 转弯，8 m/s | 转弯后 CTE < 20m |
| WOP-003 | 120° 大转弯，5 m/s | 转弯后 CTE < 25m |
| WOP-004 | 小转角 5° | 不触发 WOP，LOS 处理 |
| WOP-005 | 速度从 8→4 m/s 变化中的 WOP | WOP 距离跟随速度更新 |
| WOP-006 | CTE=20m 右偏 + 右转 | WOP 修正后偏差不恶化 |
| WOP-007 | 混合引导平滑性 | 航向变化率无阶跃 |
| WOP-008 | 转弯完成判定 | 航向<5° + 角速度<1°/s 持续 5s |
| WOP-009 | 避碰中断转弯 | 正确中止 + 切换避碰 |
| WOP-010 | 连续 S 型 4 个弯 | 多次 WOP 无累积偏差 |

---

## 参考文献

| 编号 | 文献 | 相关内容 |
|------|------|---------|
| [1] | Fossen, T.I. 2021 | 路径跟踪与转弯几何 |
| [2] | IMO MSC.137(76) | 旋回试验标准 |

---

**文档结束**
