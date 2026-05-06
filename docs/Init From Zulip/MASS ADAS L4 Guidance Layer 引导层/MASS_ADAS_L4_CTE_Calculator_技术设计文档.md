# MASS_ADAS CTE Calculator 横向偏差计算器技术设计文档

**文档编号：** SANGO-ADAS-CTE-001  
**版本：** 0.1 草案  
**日期：** 2026-04-26  
**编制：** 系统架构组  
**状态：** 开发参考

---

## 目录

1. 概述与职责定义
2. 输入与输出接口
3. 核心参数数据库
4. 船长的偏差感知思维模型
5. 计算流程总览
6. 步骤一：原始 CTE 接收与验证
7. 步骤二：CTE 滤波与噪声抑制
8. 步骤三：CTE 变化率计算
9. 步骤四：CTE 安全走廊评估
10. 步骤五：CTE 趋势分析与预测
11. 步骤六：CTE 报警与异常处理
12. 多源 CTE 融合
13. CTE 在不同水域的精度要求
14. CTE 与速度的耦合关系
15. 内部子模块分解
16. 数值算例
17. 参数总览表
18. 与其他模块的协作关系
19. 测试策略与验收标准
20. 参考文献与标准

---

## 1. 概述与职责定义

### 1.1 模块定位

CTE Calculator（横向偏差计算器，Cross-Track Error Calculator）是 Layer 4（Guidance Layer）的精度监控子模块。它接收 LOS Guidance 计算的原始 CTE 值，执行滤波、质量评估、变化率计算、安全走廊判定和趋势预测，输出高质量的 CTE 信息供整个引导层和上层决策使用。

CTE 是衡量航线跟踪质量的最核心指标——它直接告诉系统"船偏离计划航线多远"。船长在驾驶台上最常看的 ECDIS 数据之一就是 XTE（Cross-Track Error），大型商船的 ECDIS 在 XTE 超过设定阈值时会发出报警。

### 1.2 核心职责

- **CTE 滤波：** 消除 GPS 定位噪声引起的 CTE 抖动，输出平滑稳定的 CTE 值。
- **CTE 变化率计算：** 计算 de/dt，反映船偏离航线的速率和方向——正在靠近航线还是正在远离。
- **安全走廊评估：** 判定当前 CTE 是否在安全走廊范围内，是否接近走廊边界。
- **趋势预测：** 基于当前 CTE 和变化率预测未来 30~60 秒的 CTE，提前发现偏航趋势。
- **报警生成：** 当 CTE 超出警告或危险阈值时生成分级报警。

### 1.3 设计原则

- **真实反映原则：** 滤波应消除噪声但不掩盖真实偏差变化——滤波器不能太重。
- **保守报警原则：** 报警宁可多发不可漏发。CTE 接近走廊边界时即应预警。
- **低延迟原则：** CTE 是控制回路中的关键反馈量，滤波引入的延迟不应超过 1 秒。

---

## 2. 输入与输出接口

### 2.1 输入

| 输入项 | 来源 | 频率 | 说明 |
|-------|------|------|------|
| 原始 CTE (e_raw) | LOS Guidance | 10 Hz | 路径投影计算的横向偏差（米，右偏正） |
| 沿航线距离 (s) | LOS Guidance | 10 Hz | 当前投影点在航段上的位置 |
| 航段方位角 (α_k) | LOS Guidance | 10 Hz | 当前航段方向 |
| 本船速度 (u, v) | Nav Filter | 50 Hz | 纵荡和横荡速度（m/s） |
| 本船航向 (ψ) | Nav Filter | 50 Hz | 真航向（弧度） |
| 安全走廊半宽 | Voyage Planner (L2) | 事件 | RouteWaypoint.safety_corridor（米） |
| GPS 定位精度 | Nav Filter | 10 Hz | HDOP 或位置协方差 |

### 2.2 输出

```
CteOutput:
    Header header
    
    # ---- 核心 CTE 数据 ----
    float64 cte_raw                 # 原始 CTE（米，右偏为正）
    float64 cte_filtered            # 滤波后 CTE（米）
    float64 cte_rate                # CTE 变化率（m/s，正=偏离中，负=回归中）
    float64 cte_rate_filtered       # 滤波后 CTE 变化率
    
    # ---- 安全走廊评估 ----
    float64 corridor_half_width     # 当前安全走廊半宽（米）
    float64 corridor_utilization    # 走廊利用率 |CTE|/corridor（0~1，>1=超出）
    string cte_status               # "nominal"/"warning"/"critical"/"out_of_corridor"
    
    # ---- 趋势预测 ----
    float64 cte_predicted_30s       # 30 秒后预测 CTE（米）
    float64 cte_predicted_60s       # 60 秒后预测 CTE（米）
    string cte_trend                # "converging"/"stable"/"diverging"
    
    # ---- 数据质量 ----
    float64 cte_confidence          # CTE 数据置信度 [0,1]
    bool gps_quality_ok             # GPS 定位质量是否合格
```

---

## 3. 核心参数数据库

### 3.1 滤波参数

| 参数 | 符号 | 默认值 | 说明 |
|------|------|-------|------|
| CTE 低通滤波时间常数 | τ_cte | 3.0 秒 | 一阶低通滤波器 |
| CTE 变化率滤波时间常数 | τ_rate | 5.0 秒 | 变化率的额外平滑 |
| 微分计算窗口 | T_diff | 2.0 秒 | 用于计算 de/dt 的时间窗口 |
| GPS 跳变检测阈值 | CTE_jump_max | 20 m | 单周期 CTE 变化超过此值视为跳变 |

### 3.2 安全走廊参数

| 水域类型 | 默认走廊半宽 | 警告阈值（占走廊%） | 危险阈值（占走廊%） |
|---------|------------|-------------------|-------------------|
| open_sea | 500 m | 60% | 85% |
| coastal | 200 m | 60% | 85% |
| narrow_channel | 50 m | 50% | 75% |
| tss_lane | 200 m | 60% | 85% |
| port_approach | 100 m | 50% | 75% |
| port_inner | 30 m | 40% | 65% |

**注意：** 以上为默认值。实际走廊半宽由 WP Generator 根据 ENC Reader 的可用水域宽度计算，附在每个航点的 safety_corridor 字段中。

### 3.3 趋势预测参数

| 参数 | 值 | 说明 |
|------|-----|------|
| 预测时间点 | 30s, 60s | 短期和中期预测 |
| 趋势判定阈值（收敛） | de/dt < -0.5 m/s | CTE 在减小 |
| 趋势判定阈值（发散） | de/dt > 0.5 m/s | CTE 在增大 |
| 趋势稳定带 | |de/dt| ≤ 0.5 m/s | CTE 基本不变 |

---

## 4. 船长的偏差感知思维模型

### 4.1 船长如何感知偏差

船长对航线偏差的感知分三个层次：

**第一层：幅度感知——"我偏了多少？"** 他看 ECDIS 上显示的 XTE 数值或航迹相对于航线的偏离量。在开阔水域偏 100 米不算什么，但在港内偏 30 米可能就快出航道了。

**第二层：趋势感知——"我在回来还是在跑？"** 他不只看 XTE 的绝对值，更看它的变化趋势。如果 XTE 从 50 米在减小，说明修正在生效，他会安心等。如果 XTE 从 50 米在增大，他就紧张了——要么风流太大，要么舵效不够。

**第三层：预判感知——"如果不管它，待会儿会怎样？"** 经验丰富的船长能根据当前偏差和趋势，大致判断"照这个趋势，两分钟后会偏到哪里"。如果预判会超出航道，他会提前加大修正。

CTE Calculator 的三个核心输出（cte_filtered、cte_rate、cte_predicted）精确对应船长这三个层次的感知。

### 4.2 船长对不同水域的偏差容忍度

| 水域 | 船长的心理安全带 | 开始关注的偏差 | 开始担心的偏差 |
|------|----------------|-------------|-------------|
| 大洋 | 很宽，几百米不在意 | > 200 m | > 500 m |
| 沿岸 | 中等 | > 50 m | > 150 m |
| 狭水道 | 很窄 | > 15 m | > 30 m |
| 港内 | 极窄 | > 5 m | > 15 m |

CTE Calculator 的走廊参数和报警阈值就是这些"心理安全带"的量化。

---

## 5. 计算流程总览

```
输入：e_raw（原始 CTE）+ 本船状态 + 走廊参数
          │
          ▼
    ┌──────────────────────────────────┐
    │ 步骤一：原始 CTE 接收与验证       │
    │ 检查 GPS 跳变、数据时效           │
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 步骤二：CTE 滤波与噪声抑制       │
    │ 一阶低通滤波器                    │
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 步骤三：CTE 变化率计算            │
    │ 带滤波的微分计算                  │
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 步骤四：安全走廊评估              │
    │ 利用率计算 + 状态分级             │
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 步骤五：趋势分析与预测            │
    │ 线性外推 + 趋势分类              │
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 步骤六：报警与异常处理            │
    │ 分级报警生成                      │
    └────────────┬─────────────────────┘
                 │
                 ▼
输出：CteOutput
```

---

## 6. 步骤一：原始 CTE 接收与验证

### 6.1 GPS 跳变检测

GPS 定位偶尔会出现跳变（多径效应、卫星切换），导致 CTE 在一个控制周期内突变数十米。这种跳变必须检测并拒绝。

```
FUNCTION validate_cte_input(e_raw, e_raw_prev, dt):
    
    # 检查单周期变化量
    delta_e = abs(e_raw - e_raw_prev)
    
    IF delta_e > CTE_JUMP_MAX:
        # 疑似 GPS 跳变——拒绝本次数据
        log_event("cte_gps_jump_rejected", {
            delta: delta_e,
            e_raw: e_raw,
            e_prev: e_raw_prev
        })
        
        # 使用上一次有效值 + 变化率外推
        e_validated = e_raw_prev + cte_rate_filtered × dt
        confidence = 0.3    # 降低置信度
        
        # 计数连续跳变
        jump_counter += 1
        IF jump_counter > JUMP_COUNTER_MAX:
            # 连续多次跳变——可能是 GPS 故障
            log_event("gps_possible_fault", {count: jump_counter})
            confidence = 0.1
        
        RETURN {e_validated, confidence, valid: false}
    
    ELSE:
        jump_counter = 0    # 重置跳变计数
        RETURN {e_raw, confidence: compute_gps_confidence(hdop), valid: true}

CTE_JUMP_MAX = 20    # 米
JUMP_COUNTER_MAX = 5  # 连续 5 次跳变视为 GPS 故障
```

### 6.2 GPS 精度评估

```
FUNCTION compute_gps_confidence(hdop):
    
    IF hdop < 1.0:
        RETURN 1.0     # 优秀精度
    ELIF hdop < 2.0:
        RETURN 0.9     # 良好
    ELIF hdop < 5.0:
        RETURN 0.7     # 可接受
    ELIF hdop < 10.0:
        RETURN 0.4     # 较差
    ELSE:
        RETURN 0.2     # 很差
```

### 6.3 数据时效检查

```
FUNCTION check_data_freshness(e_raw_timestamp, now):
    
    data_age = now - e_raw_timestamp
    
    IF data_age > DATA_STALE_THRESHOLD:
        log_event("cte_data_stale", {age: data_age})
        RETURN {stale: true, confidence_reduction: 0.5}
    
    RETURN {stale: false}

DATA_STALE_THRESHOLD = 1.0    # 秒
```

---

## 7. 步骤二：CTE 滤波与噪声抑制

### 7.1 一阶低通滤波器

```
FUNCTION filter_cte(e_validated, e_filtered_prev, dt, τ):
    
    # 一阶低通滤波器
    α = dt / (τ + dt)
    
    e_filtered = α × e_validated + (1 - α) × e_filtered_prev
    
    RETURN e_filtered
```

**滤波时间常数 τ 的选择：**

| 场景 | τ (秒) | 理由 |
|------|--------|------|
| 高精度 GPS (HDOP < 1) | 1.5 | GPS 噪声小，快速跟随 |
| 标准 GPS (HDOP 1~3) | 3.0 | 标准滤波 |
| 低精度 GPS (HDOP > 5) | 6.0 | 强滤波抑制噪声 |
| 狭水道/港内 | 2.0 | 精度要求高，不能太慢 |
| 开阔水域 | 4.0 | 可以更平滑 |

```
FUNCTION compute_adaptive_tau(hdop, zone_type):
    
    # 基于 GPS 精度
    IF hdop < 1.0:
        τ_gps = 1.5
    ELIF hdop < 3.0:
        τ_gps = 3.0
    ELSE:
        τ_gps = 6.0
    
    # 基于水域类型
    IF zone_type IN ["narrow_channel", "port_inner"]:
        τ_zone = 2.0
    ELIF zone_type IN ["port_approach", "tss_lane"]:
        τ_zone = 3.0
    ELSE:
        τ_zone = 4.0
    
    # 取较小值（精度优先）
    RETURN min(τ_gps, τ_zone)
```

### 7.2 滤波器延迟分析

一阶低通滤波器的等效延迟约为 0.5 × τ。对于 τ = 3.0 秒，延迟约 1.5 秒。这在 LOS 的前视距离（通常 100~300m，对应 12~38 秒航行时间）尺度下可以接受。

如果延迟不可接受（比如在港内需要更快的响应），可以使用 α-β 滤波器或卡尔曼滤波器来在平滑性和响应速度之间取得更好的权衡。

### 7.3 α-β 滤波器（高级选项）

```
FUNCTION alpha_beta_filter(e_measured, e_predicted, rate_predicted, dt, α_ab, β_ab):
    
    # 预测步
    e_pred = e_predicted + rate_predicted × dt
    
    # 更新步
    residual = e_measured - e_pred
    e_updated = e_pred + α_ab × residual
    rate_updated = rate_predicted + (β_ab / dt) × residual
    
    RETURN {e_filtered: e_updated, rate_filtered: rate_updated}

# 推荐参数（基于 Benedict-Bordner 准则）
# α_ab = 0.2, β_ab = 0.01（平滑优先）
# α_ab = 0.5, β_ab = 0.05（响应优先）
```

---

## 8. 步骤三：CTE 变化率计算

### 8.1 基于滤波 CTE 的数值微分

```
FUNCTION compute_cte_rate(cte_history, T_diff):
    
    # 使用最近 T_diff 秒的数据做线性回归，斜率即为变化率
    recent = [r for r in cte_history if r.timestamp > now() - T_diff]
    
    IF len(recent) < 3:
        RETURN 0    # 数据不足
    
    times = [(r.timestamp - recent[0].timestamp) for r in recent]
    values = [r.cte_filtered for r in recent]
    
    slope = linear_regression_slope(times, values)
    
    RETURN slope    # m/s，正值=CTE 在增大（偏离），负值=CTE 在减小（回归）
```

### 8.2 基于运动学的解析变化率（交叉验证）

CTE 的变化率也可以从运动学直接计算——不依赖数值微分：

```
FUNCTION compute_cte_rate_kinematic(u, v, ψ, α_k):
    
    # 横向偏差的时间导数 = 横向速度分量（垂直于航段方向）
    # de/dt = U × sin(ψ - α_k) + v_env_cross
    
    heading_error = ψ - α_k
    de_dt = u × sin(heading_error) + v × cos(heading_error)
    # 注意：v 是横荡速度（本身就是横向分量）
    
    RETURN de_dt
```

**两种方法的融合：**

```
FUNCTION fuse_cte_rates(rate_numerical, rate_kinematic, w_numerical=0.3, w_kinematic=0.7):
    
    # 运动学计算更平滑（不受 GPS 噪声直接影响），给予更高权重
    rate_fused = w_numerical × rate_numerical + w_kinematic × rate_kinematic
    
    # 但如果两者差距很大，可能存在问题
    IF abs(rate_numerical - rate_kinematic) > RATE_DIVERGENCE_THRESHOLD:
        log_event("cte_rate_divergence", {
            numerical: rate_numerical,
            kinematic: rate_kinematic
        })
        # 信任运动学值（更鲁棒）
        rate_fused = rate_kinematic
    
    RETURN rate_fused

RATE_DIVERGENCE_THRESHOLD = 2.0    # m/s
```

### 8.3 CTE 变化率滤波

```
cte_rate_filtered = α_rate × cte_rate_raw + (1 - α_rate) × cte_rate_filtered_prev

# α_rate 比 CTE 本身的滤波更强（变化率噪声更大）
α_rate = dt / (τ_rate + dt)    # τ_rate = 5.0 秒
```

---

## 9. 步骤四：CTE 安全走廊评估

### 9.1 走廊利用率计算

```
FUNCTION evaluate_corridor(cte_filtered, corridor_half_width):
    
    # 利用率 = |CTE| / 走廊半宽
    utilization = abs(cte_filtered) / corridor_half_width
    
    RETURN utilization    # 0 = 在航线上，1 = 刚好在边界上，>1 = 超出
```

### 9.2 CTE 状态分级

```
FUNCTION classify_cte_status(utilization, zone_type):
    
    thresholds = get_corridor_thresholds(zone_type)
    
    IF utilization > 1.0:
        RETURN "out_of_corridor"    # 已超出安全走廊
    ELIF utilization > thresholds.critical_ratio:
        RETURN "critical"           # 接近走廊边界，需要加强修正
    ELIF utilization > thresholds.warning_ratio:
        RETURN "warning"            # 偏差偏大，应关注
    ELSE:
        RETURN "nominal"            # 正常范围
```

### 9.3 状态变化的迟滞处理

防止在阈值边界处频繁切换状态：

```
FUNCTION apply_hysteresis(new_status, current_status, utilization, thresholds):
    
    # 只有在利用率明确超过阈值（+ 迟滞带）时才升级状态
    # 只有在利用率明确低于阈值（- 迟滞带）时才降级状态
    
    HYSTERESIS = 0.05    # 5% 迟滞带
    
    IF new_status severity > current_status severity:
        # 升级：需要超过阈值 + 迟滞带
        IF utilization > threshold + HYSTERESIS:
            RETURN new_status
        ELSE:
            RETURN current_status    # 保持不变
    
    ELIF new_status severity < current_status severity:
        # 降级：需要低于阈值 - 迟滞带
        IF utilization < threshold - HYSTERESIS:
            RETURN new_status
        ELSE:
            RETURN current_status    # 保持不变
    
    RETURN new_status
```

---

## 10. 步骤五：CTE 趋势分析与预测

### 10.1 线性趋势预测

```
FUNCTION predict_cte(cte_filtered, cte_rate_filtered, t_predict):
    
    # 简单线性外推
    cte_predicted = cte_filtered + cte_rate_filtered × t_predict
    
    RETURN cte_predicted
```

### 10.2 趋势分类

```
FUNCTION classify_cte_trend(cte_rate_filtered):
    
    IF cte_rate_filtered < -TREND_CONVERGE_THRESHOLD:
        RETURN "converging"     # CTE 在减小——船正在回到航线
    ELIF cte_rate_filtered > TREND_DIVERGE_THRESHOLD:
        RETURN "diverging"      # CTE 在增大——船正在偏离航线
    ELSE:
        RETURN "stable"         # CTE 基本不变

TREND_CONVERGE_THRESHOLD = 0.5    # m/s
TREND_DIVERGE_THRESHOLD = 0.5     # m/s
```

### 10.3 预测超出走廊的预警

```
FUNCTION check_predicted_corridor_breach(cte_predicted_30s, cte_predicted_60s, corridor_half_width):
    
    IF abs(cte_predicted_30s) > corridor_half_width:
        RETURN {
            breach_predicted: true,
            time_to_breach: estimate_time_to_breach(cte_filtered, cte_rate_filtered, corridor_half_width),
            severity: "warning",
            message: f"预测 {time_to_breach:.0f}s 后 CTE 将超出安全走廊"
        }
    
    IF abs(cte_predicted_60s) > corridor_half_width:
        RETURN {
            breach_predicted: true,
            time_to_breach: estimate_time_to_breach(...),
            severity: "info",
            message: f"预测 60s 内 CTE 可能超出安全走廊"
        }
    
    RETURN {breach_predicted: false}

FUNCTION estimate_time_to_breach(cte, rate, corridor):
    IF rate == 0 OR sign(rate) != sign(cte):
        RETURN +∞    # 不会超出（CTE 在减小或为零）
    
    remaining = corridor - abs(cte)
    t = remaining / abs(rate)
    RETURN t
```

---

## 11. 步骤六：CTE 报警与异常处理

### 11.1 分级报警

```
FUNCTION generate_cte_alarms(cte_status, cte_trend, prediction, cte_filtered, corridor):
    
    alarms = []
    
    # 报警 1：已超出走廊
    IF cte_status == "out_of_corridor":
        alarms.append({
            level: "CRITICAL",
            code: "CTE-001",
            message: f"CTE {abs(cte_filtered):.0f}m 已超出安全走廊 {corridor:.0f}m",
            action_required: "增大修正力度或请求航线重规划"
        })
    
    # 报警 2：接近走廊边界
    IF cte_status == "critical":
        alarms.append({
            level: "WARNING",
            code: "CTE-002",
            message: f"CTE {abs(cte_filtered):.0f}m 接近安全走廊边界（走廊 {corridor:.0f}m）",
            action_required: "关注偏差趋势"
        })
    
    # 报警 3：偏差趋势发散
    IF cte_trend == "diverging" AND cte_status != "nominal":
        alarms.append({
            level: "WARNING",
            code: "CTE-003",
            message: f"CTE 正在持续增大（变化率 {cte_rate_filtered:.2f} m/s）",
            action_required: "检查偏流修正是否正确"
        })
    
    # 报警 4：预测超出走廊
    IF prediction.breach_predicted AND prediction.time_to_breach < 60:
        alarms.append({
            level: "WARNING",
            code: "CTE-004",
            message: prediction.message,
            action_required: "预防性加大修正"
        })
    
    RETURN alarms
```

### 11.2 超出走廊时的响应

当 CTE 超出安全走廊时，CTE Calculator 不直接修改航向指令（那是 LOS Guidance 的事），但它通过 cte_status 信号影响 LOS Guidance 的行为：

```
# 在 LOS Guidance 中：
IF cte_calculator.cte_status == "critical":
    # 减小前视距离以加速修正
    Δ = max(Δ × 0.5, Δ_min)

IF cte_calculator.cte_status == "out_of_corridor":
    # 最小前视距离 + 通知 Voyage Planner 评估是否需要重规划
    Δ = Δ_min
    notify_voyage_planner("CTE exceeds safety corridor")
```

### 11.3 CTE 持续超出走廊的超时处理

```
FUNCTION check_out_of_corridor_timeout(cte_status, out_of_corridor_start_time):
    
    IF cte_status == "out_of_corridor":
        IF out_of_corridor_start_time IS NULL:
            out_of_corridor_start_time = now()
        
        duration = now() - out_of_corridor_start_time
        
        IF duration > OUT_OF_CORRIDOR_TIMEOUT:
            # 超出走廊太久——LOS 无法自行修正
            # 请求 Voyage Planner 重新规划航线
            request_route_replan(
                reason: f"CTE 超出安全走廊超过 {duration:.0f}s，LOS 引导无法回归",
                current_cte: cte_filtered,
                corridor: corridor_half_width
            )
    ELSE:
        out_of_corridor_start_time = NULL    # 重置

OUT_OF_CORRIDOR_TIMEOUT = 120    # 秒（2 分钟）
```

---

## 12. 多源 CTE 融合

### 12.1 GPS CTE 与航迹推算 CTE

除了 GPS 定位计算的 CTE 之外，还可以通过航迹推算（Dead Reckoning, DR）独立计算 CTE。DR 基于 IMU 和计程仪数据，不依赖 GPS——在 GPS 信号质量差或丢失时作为备份。

```
FUNCTION compute_dr_cte(dr_position, wp_from, wp_to, α_k):
    
    Δx = dr_position.x - wp_from.x
    Δy = dr_position.y - wp_from.y
    
    e_dr = Δx × cos(α_k) - Δy × sin(α_k)
    
    RETURN e_dr
```

### 12.2 融合策略

```
FUNCTION fuse_cte_sources(e_gps, e_dr, gps_confidence):
    
    IF gps_confidence > 0.7:
        # GPS 良好——以 GPS 为主
        w_gps = 0.9
        w_dr = 0.1
    ELIF gps_confidence > 0.3:
        # GPS 一般——混合
        w_gps = 0.5
        w_dr = 0.5
    ELSE:
        # GPS 很差——以 DR 为主
        w_gps = 0.1
        w_dr = 0.9
    
    e_fused = w_gps × e_gps + w_dr × e_dr
    
    # 两者偏差过大时告警
    IF abs(e_gps - e_dr) > GPS_DR_DIVERGENCE_THRESHOLD:
        log_event("gps_dr_divergence", {gps: e_gps, dr: e_dr, diff: abs(e_gps - e_dr)})
    
    RETURN e_fused

GPS_DR_DIVERGENCE_THRESHOLD = 50    # 米
```

### 12.3 GPS 丢失时的纯 DR 模式

```
IF gps_confidence < 0.1 OR gps_data_stale:
    # GPS 完全不可用——切换到纯航迹推算
    e_used = e_dr
    cte_confidence = max(0.3, 1.0 - dr_drift_time / 300)
    # DR 置信度随时间衰减（每 5 分钟降至 0.3）
    
    log_event("cte_gps_lost_using_dr", {dr_confidence: cte_confidence})
```

---

## 13. CTE 在不同水域的精度要求

### 13.1 精度等级定义

| 精度等级 | 稳态 CTE 要求 | 适用水域 |
|---------|-------------|---------|
| 航海级（Navigation） | < 100 m | 开阔水域、沿岸 |
| 航道级（Channel） | < 20 m | 狭水道、TSS |
| 港口级（Harbour） | < 5 m | 港内、码头接近 |
| 靠泊级（Berthing） | < 1 m | 靠泊操纵（需 RTK GPS） |

### 13.2 精度与 GPS 的关系

| GPS 模式 | 典型精度 (CEP) | 可达到的 CTE 精度 |
|---------|---------------|-----------------|
| 标准 GPS | 2~5 m | 航海级（> 5m） |
| SBAS/WAAS | 1~2 m | 航道级（2~5m） |
| DGPS | 0.5~1 m | 港口级（1~3m） |
| RTK GPS | 0.02~0.05 m | 靠泊级（< 0.5m） |

CTE Calculator 应根据当前 GPS 模式自动调整滤波参数和报警阈值——不要求标准 GPS 达到港口级精度，那是不可能的。

```
FUNCTION adjust_for_gps_mode(gps_mode, zone_type):
    
    achievable_accuracy = GPS_ACCURACY[gps_mode]
    required_accuracy = ZONE_REQUIREMENT[zone_type]
    
    IF achievable_accuracy > required_accuracy × 0.5:
        # GPS 精度不足以满足当前水域要求
        log_event("gps_accuracy_insufficient", {
            achievable: achievable_accuracy,
            required: required_accuracy,
            zone: zone_type
        })
        # 增大走廊阈值（降低报警灵敏度）
        # 通知 Shore Link 建议升级 GPS 或减速
```

---

## 14. CTE 与速度的耦合关系

### 14.1 高速时的 CTE 特性

高速时 CTE 的特征：
- GPS 采样之间的航行距离更大，CTE 的有效更新率降低。
- 船的惯性大，修正响应慢，CTE 收敛时间长。
- 海流的相对影响减小（船速 >> 流速时偏流角小）。

```
# 高速时自动放宽走廊阈值
IF own_speed > HIGH_SPEED_THRESHOLD:
    corridor_effective = corridor_half_width × (1 + 0.1 × (own_speed - HIGH_SPEED_THRESHOLD))
    # 每超过阈值 1 m/s，走廊放宽 10%

HIGH_SPEED_THRESHOLD = 8.0    # m/s
```

### 14.2 低速时的 CTE 特性

低速时 CTE 的特征：
- 舵效降低，修正能力变差。
- 海流的相对影响增大（可能导致无法维持航线）。
- 侧推器生效（低速时侧推有效），可以辅助横向修正。

```
# 低速时如果 CTE 持续增大且 LOS 修正无效
IF own_speed < LOW_SPEED_THRESHOLD AND cte_trend == "diverging":
    # 通知 Control 层考虑启用侧推辅助
    request_lateral_thrust_assist(cte_filtered, cte_rate_filtered)

LOW_SPEED_THRESHOLD = 2.0    # m/s
```

---

## 15. 内部子模块分解

| 子模块 | 职责 | 建议语言 |
|-------|------|---------|
| Input Validator | GPS 跳变检测、数据时效检查 | C++ |
| CTE Filter | 一阶低通/α-β 滤波 + 自适应 τ | C++ |
| Rate Calculator | CTE 变化率计算（数值微分 + 运动学融合） | C++ |
| Corridor Evaluator | 安全走廊利用率计算 + 状态分级 + 迟滞 | C++ |
| Trend Predictor | 线性外推预测 + 趋势分类 + 超出预警 | C++ |
| Alarm Generator | 分级报警生成 | C++ |
| Multi-Source Fuser | GPS/DR CTE 融合 | C++ |
| Timeout Monitor | 超出走廊超时处理 + 重规划请求 | C++ |

---

## 16. 数值算例

### 16.1 算例一：正常跟踪——CTE 滤波效果

**条件：**

```
航线：正北方向
GPS 精度：HDOP = 1.5（CEP ≈ 2m）
实际 CTE = 5.0 m（恒定，船平稳跟踪）
GPS 噪声叠加：±2m 高斯噪声
滤波 τ = 3.0s，更新率 10 Hz（dt = 0.1s）
α = 0.1 / (3.0 + 0.1) = 0.032
```

**原始 CTE 序列（含噪声）：**

```
t=0.0s: e_raw = 4.2m
t=0.1s: e_raw = 6.1m
t=0.2s: e_raw = 3.8m
t=0.3s: e_raw = 5.5m
t=0.4s: e_raw = 7.3m  ← 噪声突刺
t=0.5s: e_raw = 4.9m
```

**滤波后 CTE：**

```
t=0.0s: e_filtered = 0.032×4.2 + 0.968×5.0 = 4.97m
t=0.1s: e_filtered = 0.032×6.1 + 0.968×4.97 = 5.01m
t=0.2s: e_filtered = 0.032×3.8 + 0.968×5.01 = 4.97m
t=0.3s: e_filtered = 0.032×5.5 + 0.968×4.97 = 4.99m
t=0.4s: e_filtered = 0.032×7.3 + 0.968×4.99 = 5.06m  ← 噪声被大幅抑制
t=0.5s: e_filtered = 0.032×4.9 + 0.968×5.06 = 5.05m
```

**结果：** 原始噪声幅度 ±2m 被抑制到 ±0.06m，有效抑制比约 30:1。

### 16.2 算例二：走廊评估——狭水道

```
CTE_filtered = 18 m（右偏）
走廊半宽 = 50 m（狭水道）
zone_type = "narrow_channel"

利用率 = 18 / 50 = 0.36
狭水道警告阈值 = 0.50，危险阈值 = 0.75

状态 = "nominal"（0.36 < 0.50）

CTE 变化率 = +0.8 m/s（偏离中）

预测 30s 后 CTE = 18 + 0.8 × 30 = 42 m
利用率预测 = 42 / 50 = 0.84 > 0.75
→ 预测将超出危险阈值！

报警：CTE-004 "预测约 40 秒后 CTE 将接近走廊边界"
```

### 16.3 算例三：GPS 跳变

```
t=10.0s: e_raw = 12.3m, e_filtered = 12.1m
t=10.1s: e_raw = 45.8m  ← GPS 跳变！

delta_e = |45.8 - 12.3| = 33.5m > CTE_JUMP_MAX(20m)
→ 跳变拒绝！

使用外推值：e_validated = 12.1 + 0.3 × 0.1 = 12.13m（假设 rate = 0.3 m/s）
e_filtered = 0.032 × 12.13 + 0.968 × 12.1 = 12.10m

→ CTE 输出不受 GPS 跳变影响
```

---

## 17. 参数总览表

| 类别 | 参数 | 默认值 | 说明 |
|------|------|-------|------|
| **滤波** | CTE 滤波时间常数 τ_cte | 3.0 s | 自适应调整 |
| | CTE 变化率滤波时间常数 τ_rate | 5.0 s | |
| | 微分计算窗口 T_diff | 2.0 s | |
| | 运动学变化率权重 | 0.7 | |
| | 数值微分变化率权重 | 0.3 | |
| **验证** | GPS 跳变检测阈值 | 20 m | |
| | 连续跳变故障阈值 | 5 次 | |
| | 数据过期阈值 | 1.0 s | |
| | GPS/DR 偏差告警阈值 | 50 m | |
| **走廊** | 开阔水域默认半宽 | 500 m | 被 WP Generator 覆盖 |
| | 狭水道默认半宽 | 50 m | |
| | 港内默认半宽 | 30 m | |
| | 警告利用率（开阔） | 60% | |
| | 危险利用率（开阔） | 85% | |
| | 警告利用率（狭水道） | 50% | |
| | 危险利用率（狭水道） | 75% | |
| | 迟滞带 | 5% | |
| | 超出走廊超时 | 120 s | |
| **趋势** | 收敛趋势阈值 | -0.5 m/s | |
| | 发散趋势阈值 | +0.5 m/s | |
| | 预测时间点 | 30s, 60s | |
| **速度耦合** | 高速走廊放宽起始速度 | 8.0 m/s | |
| | 低速侧推辅助起始速度 | 2.0 m/s | |

---

## 18. 与其他模块的协作关系

| 交互方 | 方向 | 数据内容 | 频率 |
|-------|------|---------|------|
| LOS Guidance → CTE Calc | 输入 | e_raw, s, α_k | 10 Hz |
| Nav Filter → CTE Calc | 输入 | u, v, ψ, HDOP | 50/10 Hz |
| Voyage Planner → CTE Calc | 输入 | safety_corridor（走廊半宽） | 事件 |
| CTE Calc → LOS Guidance | 反馈 | cte_filtered, cte_status（影响 Δ 调整） | 10 Hz |
| CTE Calc → Heading Anticipation | 输出 | cte_filtered, cte_rate_filtered | 10 Hz |
| CTE Calc → Look-ahead Speed | 输出 | cte_status（大偏差时建议降速） | 10 Hz |
| CTE Calc → Shore Link | 输出 | 报警信息 | 事件 |
| CTE Calc → Voyage Planner | 请求 | 超出走廊超时→重规划请求 | 事件 |

---

## 19. 测试策略与验收标准

### 19.1 测试场景

| 场景编号 | 描述 | 验证目标 |
|---------|------|---------|
| CTE-001 | 恒定 CTE + GPS 噪声 | 滤波抑制噪声、保留真值 |
| CTE-002 | CTE 阶跃变化 | 滤波跟随速度（延迟 < 1s） |
| CTE-003 | GPS 跳变 50m | 跳变检测 + 拒绝 |
| CTE-004 | GPS 连续跳变 | 连续跳变故障检测 |
| CTE-005 | GPS 丢失 | 切换到 DR 模式 |
| CTE-006 | CTE 从 0 增大到走廊边界 | 状态分级：nominal→warning→critical |
| CTE-007 | CTE 超出走廊 | out_of_corridor 状态 + CRITICAL 报警 |
| CTE-008 | CTE 超出走廊 > 120s | 重规划请求触发 |
| CTE-009 | CTE 在阈值边界波动 | 迟滞处理正确（不频繁切换） |
| CTE-010 | CTE 趋势发散预测 | 30s 预测正确 + 预警触发 |
| CTE-011 | 运动学变化率 vs 数值微分 | 两者一致性 + 融合正确 |
| CTE-012 | 高速航行（15 节） | 走廊自动放宽 |
| CTE-013 | 低速航行（3 节）+ CTE 发散 | 侧推辅助请求 |
| CTE-014 | 不同水域类型切换 | 走廊和阈值自动调整 |
| CTE-015 | GPS HDOP 变化 | 滤波 τ 自适应调整 |

### 19.2 验收标准

| 指标 | 标准 |
|------|------|
| 滤波噪声抑制比（2m GPS 噪声） | > 20:1（输出波动 < 0.1m） |
| 滤波跟随延迟 | < 1.5 秒（CTE 阶跃响应） |
| GPS 跳变检测率 | 100%（跳变 > 20m 的情况） |
| GPS 跳变误检率 | < 1%（正常变化被误判为跳变） |
| 走廊状态分级准确性 | 100%（与利用率计算一致） |
| 趋势预测误差（30s） | < 30%（线性趋势场景下） |
| 超出走廊超时触发可靠性 | 100% |
| 计算延迟 | < 0.5ms |

---

## 20. 参考文献与标准

| 编号 | 文献/标准 | 相关内容 |
|------|---------|---------|
| [1] | IMO A.817(19)/MSC.232(82) | ECDIS 性能标准（XTE 报警要求） |
| [2] | IEC 61162-1 | 航海电子设备数字接口（XTE 语句格式） |
| [3] | Fossen, T.I. "Handbook of Marine Craft Hydrodynamics" 2021 | LOS 引导与 CTE 理论 |
| [4] | Brown, R.G. & Hwang, P.Y.C. "Introduction to Random Signals and Applied Kalman Filtering" | 滤波理论 |
| [5] | IMO Resolution A.915(22) | 海上无线电导航系统精度要求 |

---

## v2.0 架构升级：提升为 Corridor Guard 角色

### A. Doer-Checker 定位

在 v2.0 防御性架构中，CTE Calculator 的安全走廊评估功能被提升为 **Corridor Guard**——Deterministic Checker 的 L4 层实例。

**变更前（v1.0）：** CTE Calculator 是 LOS Guidance 的辅助模块——计算 CTE、评估走廊利用率、发出报警。走廊评估的结果仅是"信息提示"，不阻止 LOS 的航向输出。

**变更后（v2.0）：** Corridor Guard 对 LOS Guidance（Doer）输出的 ψ_d 做独立校验——如果执行 ψ_d 会导致船偏出安全走廊或驶向浅水区，Corridor Guard 有权否决并输出安全替代航向。

### B. Corridor Guard 校验逻辑

```
FUNCTION corridor_guard_check(psi_d, own_ship, corridor, enc_data):
    
    # 校验 1：当前航向指令是否会导致 CTE 超出走廊
    # 预测执行 ψ_d 后 10 秒的 CTE
    predicted_cte_10s = cte_filtered + cte_rate_for_heading(psi_d, own_ship) × 10
    
    IF abs(predicted_cte_10s) > corridor.half_width × 0.95:
        RETURN {
            approved: false,
            violation: "航向指令将导致 CTE 超出走廊",
            nearest_compliant_heading: compute_corridor_safe_heading(
                own_ship, corridor, cte_filtered
            )
        }
    
    # 校验 2：航向指令是否指向浅水区域
    # 从当前位置沿 ψ_d 方向前看 500m，检查水深
    check_point = offset_point(own_ship.position, psi_d, 500)
    depth_at_check = enc_data.get_depth(check_point)
    
    IF depth_at_check < own_ship.draft + UKC_MIN:
        RETURN {
            approved: false,
            violation: f"航向 {psi_d:.1f}° 方向 500m 处水深 {depth_at_check:.1f}m 不足",
            nearest_compliant_heading: compute_depth_safe_heading(
                own_ship, enc_data, psi_d
            )
        }
    
    RETURN {approved: true}

FUNCTION compute_corridor_safe_heading(own_ship, corridor, cte):
    # 计算使 CTE 收敛的最保守航向
    # 如果 CTE > 0（右偏），安全航向应偏向左（α_k 减少修正角）
    correction = -sign(cte) × min(abs(cte) / corridor.half_width × 30, 45)
    RETURN normalize_0_360(alpha_k + correction)
```

### C. 与 LOS Guidance 的集成

```
# 在 LOS Guidance 输出 ψ_d 后、传递给 Heading Anticipation 之前

ψ_d = los_guidance.compute()                      # Doer 输出
guard_result = corridor_guard.check(ψ_d, ...)      # Checker 校验

IF guard_result.approved:
    ψ_d_final = ψ_d
ELSE:
    ψ_d_final = guard_result.nearest_compliant_heading
    asdr_publish("checker_veto", {layer: "L4", checker: "Corridor_Guard", ...})

# ψ_d_final 传递给 Heading Anticipation → ψ_cmd → L5
```

### D. 与 Deterministic Checker 总体设计的关系

| Checker 层级 | 名称 | 说明 |
|-------------|------|------|
| L2 | Route Safety Gate | 航线级检查（等深线、禁区） |
| L3 | COLREGs Compliance | 避碰决策合规检查 |
| **L4** | **Corridor Guard** | **实时航向指令安全检查（本模块）** |
| L5 | Actuator Envelope | 执行机构物理极限检查 |

---

## 变更记录

| 版本 | 日期 | 作者 | 变更内容 |
|------|------|------|---------|
| 0.1 | 2026-04-26 | 架构组 | 初始版本 |
| 0.2 | 2026-04-26 | 架构组 | v2.0 升级：提升为 Corridor Guard（Deterministic Checker L4 层实例）；增加航向指令安全校验逻辑；增加浅水区检测；增加否决后安全替代航向输出 |

---

**文档结束**
