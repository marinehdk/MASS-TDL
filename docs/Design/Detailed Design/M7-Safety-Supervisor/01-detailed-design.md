# M7 Safety Supervisor 详细功能设计

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-DD-M7-001 |
| 版本 | v1.0（**正式**）|
| 日期 | 2026-05-06 |
| 状态 | **正式**（v1.1.1 + RFC-003 + RFC-005 已批准 — 接口跨团队锁定）|
| 架构基线 | **v1.1.2**（含 RFC 决议 patch；章节锚点：§11 / §11.2–§11.9 / §11.9.2 / §15.1 / §15.2）|
| RFC 基线 | **RFC-003 已批准**（M7↔X-axis Checker enum 协议 + 100 周期 = 15 s 滑窗）+ **RFC-005 已批准**（Reflex Arc SIL 3 + 触发阈值 [HAZID 校准]）|
| 关联 finding | F-P0-D3-001 / F-P0-D3-002 / F-P2-D3-036 / F-NEW-001 / F-NEW-002 / F-NEW-005 / F-NEW-006 |
| 上游依赖 | M1 ODD_StateMsg · M2 World_StateMsg · M6 COLREGs_ConstraintMsg · M5 AvoidancePlanMsg · X-axis CheckerVetoNotification（enum）|
| 下游接口 | M1 Safety_AlertMsg · ASDR ASDR_RecordMsg · M8 SAT_DataMsg |

> **v1.0 正式版声明**：~~本详细设计基于 v1.1.1 + RFC-003 + RFC-005 提议方案；待 X-axis Checker 团队 + Reflex Arc 团队跨团队对齐确认~~ → **2026-05-06 RFC 决议归档完成**（详见 `docs/Design/Cross-Team Alignment/RFC-decisions.md`）。所有跨团队接口已锁定。Reflex Arc 团队改造 6 周；Deterministic Checker 团队改造 3 周。可立即进入实现阶段。

---

## 1. 模块职责（Responsibility）

**M7 Safety Supervisor** 是 TDL 的独立运行时安全仲裁器（L3 内部 Checker），遵循 Doer-Checker 模式（§2.5 决策四）。M7 不持有规划逻辑（不动态生成轨迹），仅通过两条独立轨道（IEC 61508 + ISO 21448 SOTIF）监控 M1–M6 Doer 的假设违反，当检测到功能不足或组件失效时，向 M1 发送安全告警并推荐预定义 MRM 命令集（v1.1.1 §11.6）。

**核心职责清单**（v1.1.1 §11.1 + §11.2）：

1. **IEC 61508 轨道（组件失效监控）**：
   - 模块心跳检测（Watchdog）— M1/M2/M3/M4/M5/M6/M8 周期信号监控
   - SIL 2 诊断覆盖 — 关键输出一致性校验（CPA / 目标列表 / ODD 状态）

2. **ISO 21448 SOTIF 轨道（功能不足监控）**：
   - 假设违反检测（6 项）— AIS/雷达一致性 / 运动可预测性 / 感知覆盖 / COLREGs 可解析性 / 通信链路 / Checker 否决率
   - 性能退化检测 — CPA 趋势恶化、预测误差超限
   - 触发条件识别 — 未知/极端场景的边界检测

3. **Safety Arbitrator（仲裁器）**：
   - 双轨信号汇聚 → Safety_AlertMsg（含 recommended_mrm 索引）
   - 向 M1 报告，M1 仲裁后下发 MRM 命令（M7 不直接向 M5 注入）（v1.1 修订，F-P0-D3-001）

4. **SOTIF/IEC 61508 双轨独立性**：
   - 两条轨道代码实现完全独立（不共享库 / 数据结构 / 失效模式）
   - 与 M2/M5/M6 实现路径独立（不共享 CPA 计算库、COLREGs 推理库）（决策四 §2.5）
   - 与 X-axis Deterministic Checker 通过受限枚举通信（不解析自由文本）（RFC-003 + F-NEW-002）

**核心安全属性**：

- **SIL 2 认证路径**（v1.1.1 §11.4）：单通道 HFT=0 + DC ≥ 90% + SFF ≥ 60%（Type A）；关键路径推荐 HFT=1 双通道 + SFF ≥ 90%
- **失效模式**：Fail-Safe（**必须**）—— M7 失效 → 强制触发保守 MRM-01；**禁止 Fail-Silent**
- **失效后降级**：M7 失效 → 系统自动降级到 D2（船上有海员备援）；禁止 D3/D4 运行

---

## 2. 输入接口（Input Interfaces）

### 2.1 消息列表

| 消息 | 来源 | 频率 | 必备字段 | 容错处理 |
|---|---|---|---|---|
| **ODD_StateMsg** | M1 | 1 Hz | auto_level (D2/D3/D4) · ood_zone (A/B/C/D) · health (FULL/DEGRADED/CRITICAL) · conformance_score · tmr_seconds · tdl_seconds | 5 s 无更新 → 降级到 CRITICAL；使用最后已知值 |
| **World_StateMsg** | M2 | 4 Hz | targets[]{cpa_nm, tcpa_sec, confidence, age_sec} · fusion_status · fusion_confidence | 缺失 > 5 s → CPA 假设违反升级；Fusion_status = DEGRADED 时 CPA_safe = 0.5 nm 强制告警 |
| **COLREGs_ConstraintMsg** | M6 | 2 Hz | applicable_rules[] · rule_violations[] · confidence · processing_success | 缺失 > 5 s 或 processing_success=false 连续 3 次 → 触发 COLREGs 不可解析告警 |
| **AvoidancePlanMsg** | M5 | 1–2 Hz | plan_status (VALID/INVALID/TIMEOUT) · confidence · execution_confidence | plan_status = TIMEOUT → 触发 M5 超时监控；缺失 > 10 s 按 CRITICAL 处理 |
| **CheckerVetoNotification** | X-axis Checker | 事件 | stamp · veto_reason_class (enum VetoReasonClass) · veto_reason_detail · vetoed_module | 累计 100 周期窗口统计，枚举分类做聚合（RFC-003 § 2.3）|
| **ReflexActivationNotification** | Reflex Arc | 事件 | activation_time · reason · l3_freeze_required | 暂停主仲裁线程；保留降级监测线程（RFC-005 + v1.1.1 §11.9） |
| **OverrideActiveSignal** | Hardware Override Arbiter | 事件 | override_active (bool) | override_active=true → M7 进入接管模式；false → 触发回切顺序化（v1.1.1 §11.9.2 — F-NEW-006） |

### 2.2 输入数据校验

**时间戳与延迟监控**：

```
所有消息强制携带 stamp（采样时间）+ received_stamp（接收时间）
延迟 = received_stamp - stamp
告警阈值（初始值，[HAZID 校准]）：
  · ODD_StateMsg：延迟 > 500 ms → 降级到 CRITICAL
  · World_StateMsg：延迟 > 300 ms 或 4 次连续缺失（> 1 s）→ 融合降级
  · COLREGs_ConstraintMsg：延迟 > 1 s 或连续 3 次缺失 → COLREGs 不可解析
  · AvoidancePlanMsg：缺失 > 10 s → M5 超时
```

**置信度门控**：

- **M2 World_StateMsg.fusion_confidence**：
  - ≥ 0.8 → 使用融合 CPA / TCPA 值
  - 0.5–0.8 → 加大 CPA 安全边距 × 1.3（保守假设）
  - < 0.5 → 触发 SOTIF 假设违反 "感知覆盖充分性"（整体降级）

- **M6 COLREGs_ConstraintMsg.confidence**：
  - ≥ 0.8 → 规则可信，按 Rule 执行
  - 0.5–0.8 → Rule 冲突标注，触发 M7 告警；M7 推荐降速观察
  - < 0.5 或 Rule violation list 非空 → "COLREGs 不可解析" 假设违反

**数据有效性校验**：

```
对每条消息验证：
1. 时间戳单调递增（允许少量乱序，窗口 ± 100 ms）
2. 数值范围（CPA ∈ [0, 10] nm；TCPA ∈ [-60, 600] s；confidence ∈ [0, 1]）
3. 数组长度（targets[] ≤ 200；rule_violations[] ≤ 50）
4. 枚举值有效性（ODD_Zone ∈ {A, B, C, D}；health ∈ {FULL, DEGRADED, CRITICAL}）

越界 → 标记数据为 INVALID；触发对应降级路径
```

---

## 3. 输出接口（Output Interfaces）

### 3.1 消息列表

| 消息 | 目标 | 频率 | 关键字段 | SLA 时延 |
|---|---|---|---|---|
| **Safety_AlertMsg** | M1 | 事件 + 周期 | type ∈ {IEC61508_FAULT, SOTIF_ASSUMPTION_VIOLATION, PERFORMANCE_DEGRADATION} · severity ∈ {WARNING, CRITICAL, EMERGENCY} · recommended_mrm ∈ {MRM-01, MRM-02, MRM-03, MRM-04} · confidence · rationale | **< 100 ms**（从检测到 M1 收到）|
| **ASDR_RecordMsg** | ASDR | 事件 + 2 Hz | timestamp · sotif_alert (list) · iec61508_fault (list) · mrm_recommended · checker_veto_count / window · confidence_breakdown | **< 50 ms** + 周期 2 Hz buffer |
| **SAT_DataMsg** | M8 | 10 Hz | sat_1_state (current_alerts, alert_count) · sat_2_breakdown (assumption_status, perf_trend) · sat_3_forecast (next_alert_eta_s, confidence_trend) | **< 100 ms** + 无缓存掉帧（实时性要求 SAT-1 优先） |

### 3.2 输出 SLA

**Safety_AlertMsg 发送 SLA**：

- **检测延迟**：从输入事件到 Alert 生成 **≤ 100 ms**（相对于输入 stamp）
- **多路诊断**：多条假设违反同时触发时，汇聚为单条 Alert（避免 M1 过载）；优先级：Emergency > Critical > Warning
- **推荐 MRM 稳定性**：30 s 内不切换 MRM（防止命令振荡）；若需切换，需再次确认条件且 confidence > 0.9
- **重复发送门槛**：同一 Alert 在 Severity 升级前不重复发送；Severity 升级（如 WARNING → CRITICAL）允许重复

**ASDR_RecordMsg 记录 SLA**：

- **实时记录**：所有 Alert + checker_veto 事件实时写入 ASDR（用于事后审计）
- **聚合窗口**：100 周期（≈ 10–20 s，[HAZID 校准]）计算 checker_veto_count；达到率 > 20% 时标注为 "Checker 否决率异常"
- **数据持久化**：ASDR 需支持回放/查询每个 M7 Alert 的完整推导链（包含输入快照 + 中间计算 + 输出）

**SAT_DataMsg 实时性保证**：

- **SAT-1 显示**：低延迟 < 100 ms；当 TDL 压缩时升级到 **极高优先级**（v1.1.1 §12.2 自适应触发）
- **SAT-2 推理**：仅在 SOTIF 告警激活 / 操作员请求时推送（不全时展示）
- **SAT-3 预测**：当 TDL < 30 s 时自动加粗 + 加大字体推送 M8

---

## 4. 内部状态（Internal State）

### 4.1 状态变量

```cxx
// 双轨监控状态
struct M7_State {
  // === 公共状态 ===
  enum OperatingMode { NORMAL, OVERRIDDEN, MRC_ACTIVE, DEGRADED, FAILED } mode;
  timestamp last_input_stamp;
  uint32_t watchdog_heartbeat_count;
  
  // === IEC 61508 轨道状态 ===
  struct IEC61508_State {
    bool m1_heartbeat_ok, m2_heartbeat_ok, m3_heartbeat_ok, m4_heartbeat_ok, 
         m5_heartbeat_ok, m6_heartbeat_ok, m8_heartbeat_ok;
    uint32_t heartbeat_loss_count[8];  // 各模块失联计数
    float last_cpa_nm, last_tcpa_sec;  // 最后一次有效 CPA/TCPA 快照
    uint32_t cpa_input_timeout_count;  // 连续无效输入计数
  } iec61508;
  
  // === SOTIF 轨道状态 ===
  struct SOTIF_State {
    // 6 项假设违反检测
    struct {
      bool ais_radar_mismatch;      // AIS/雷达位置残差 > 2σ × 10 s
      float range_residual_m;        // 融合目标残差（m）
      
      bool motion_unpredictable;     // 预测残差 RMS > 50 m / 30 s
      float pred_rmse_m;             // 当前预测误差
      
      bool perception_coverage_poor; // 盲区 > 20% of 360°
      float blind_zone_fraction;     // 盲区占比
      
      bool colregs_unsolvable;       // COLREGs 连续失败 3 次
      uint32_t colregs_fail_count;   // 连续失败计数
      
      bool comm_degraded;            // RTT > 2 s 或丢包 > 20%
      float rtt_sec, packet_loss_pct;
      
      bool checker_veto_high;        // 否决率 > 20% / 100 周期
      uint32_t veto_count_window[100]; // 滑动窗口（circular buffer）
    } assumption_violation;
    
    // 性能退化检测
    struct {
      bool cpa_trend_degrading;      // CPA 单调递减 > 30 s
      float cpa_trend_slope;         // CPA 变化率（nm/s）
      float max_cpa_in_window;       // 最大 CPA（反映恶化趋势）
      
      bool multiple_targets_nearby;  // ≥ 2 目标同时 CPA < 1.0 nm
      uint32_t critical_target_count;
    } performance;
    
    // 触发条件识别
    bool extreme_scenario_detected;  // 多条假设同时违反
    uint32_t violation_count;        // 当前周期违反项数
  } sotif;
  
  // === Checker 协调状态 ===
  struct CheckerCoordination {
    uint32_t veto_count_total;
    uint32_t veto_window_start_idx;  // 滑动窗口起始索引
    map<VetoReasonClass, uint32_t> veto_histogram;  // 枚举分类统计
  } checker;
  
  // === MRM 触发状态 ===
  struct MRMState {
    enum RecommendedMRM { NONE, MRM_01, MRM_02, MRM_03, MRM_04 } last_recommendation;
    timestamp last_mrm_change_time;
    uint32_t mrm_recommendation_count;  // 避免振荡计数
  } mrm;
  
  // === 降级路径状态 ===
  enum HealthStatus { FULL, DEGRADED, CRITICAL } health;
  uint32_t alert_queue_size;
  bool sotif_main_arbitration_paused;  // 接管模式标志
  bool sotif_degraded_monitoring_active; // 接管期间降级监测线程
};
```

### 4.2 状态机（if applicable）

```mermaid
stateDiagram-v2
    [*] --> NORMAL : 启动 + 所有输入 OK

    NORMAL --> DEGRADED : 单个假设违反 / 组件延迟 ≥ 100 ms
    NORMAL --> FAILED : 心跳丢失 > 3 周期 或 M7 自身异常

    DEGRADED --> NORMAL : 故障恢复 + 再次验证 OK（等待 5 s）
    DEGRADED --> CRITICAL : 多个假设同时违反 或 SOTIF 极端场景
    DEGRADED --> MRC_ACTIVE : 触发 MRM 指令

    CRITICAL --> MRC_ACTIVE : 紧急升级 MRM → M1
    CRITICAL --> DEGRADED : 部分故障修复 + 再验证
    CRITICAL --> FAILED : M7 自身失效

    MRC_ACTIVE --> NORMAL : 人工接管完成 + 回切顺序执行完毕（v1.1.1 §11.9.2）
    MRC_ACTIVE --> MRC_ACTIVE : MRM 命令执行中

    FAILED --> [*] : 系统停机（系统级 Fail-Safe）

    NORMAL -.->|ReflexActivationNotification| OVERRIDDEN
    OVERRIDDEN -.->|override_active=false| NORMAL : 回切顺序化（v1.1.1 §11.9.2）

    note right of DEGRADED
        心跳监控 / 融合降质
        → 降级到 DEGRADED
        保留 SOTIF 监控
    end note

    note right of CRITICAL
        多条假设同时违反
        或极端场景识别
        → 升级 Critical 级 Alert
    end note

    note right of OVERRIDDEN
        人工接管激活时：
        - 暂停主仲裁线程
        - 保留降级监测线程 (F-NEW-005)
        - 接管期间告警实时显示 < 100 ms
    end note
```

### 4.3 持久化（ASDR 记录）

M7 需将以下状态持久化到 ASDR：

| 字段 | 记录频率 | 用途 |
|---|---|---|
| **Safety_AlertMsg** 完整副本 | 事件 + 2 Hz | 告警历史追溯 |
| **假设违反诊断链** | 事件 | 每次 Alert 记录 assumption_violation 的完整状态快照 |
| **Checker veto histogram** | 100 周期 | 跨层协调统计（L2/L3/L4/L5 Checker 否决率聚合） |
| **M7 mode 状态转移** | 状态变化 | 审计轨迹（何时从 NORMAL → DEGRADED → CRITICAL） |
| **回切顺序时间戳** | 事件 | v1.1.1 §11.9.2 的严格顺序化验证 (F-NEW-006) |
| **降级监测线程告警** | 接管期间 @ 10 Hz | 接管时 M7 活性证明（F-NEW-005） |

---

## 5. 核心算法（Core Algorithm）

### 5.1 双轨架构算法概览

M7 采用**同步双轨**设计：

```
┌─────────────────────────────────────────────────────────────────┐
│                    M7 Safety Supervisor                          │
├─────────────────────────────────────────────────────────────────┤
│                                                                   │
│  ┌─────────────────────────┐    ┌──────────────────────────┐    │
│  │  Track 1: IEC 61508    │    │   Track 2: SOTIF (ISO)   │    │
│  │  (组件失效监控)        │    │   (功能不足监控)         │    │
│  │                         │    │                          │    │
│  │ • Watchdog             │    │ • 6x 假设违反检测        │    │
│  │   (模块心跳)           │    │ • 性能退化检测           │    │
│  │ • 诊断覆盖             │    │ • 触发条件识别           │    │
│  │   (输出一致性)         │    │ • Checker 协调           │    │
│  │                         │    │                          │    │
│  └────────────┬────────────┘    └──────────────┬───────────┘    │
│               │                                │                  │
│               └────────────────┬───────────────┘                  │
│                                ↓                                   │
│                    ┌──────────────────────┐                       │
│                    │  Safety Arbitrator   │                       │
│                    │  (Alert 汇聚)        │                       │
│                    └──────────┬───────────┘                       │
│                               ↓                                    │
│                    Safety_AlertMsg + MRM                          │
│                         → M1 仲裁                                  │
│                                                                   │
└─────────────────────────────────────────────────────────────────┘
```

**执行流程**：

1. **周期主线程**（10 Hz 或 4 Hz 对齐 M2）：
   - 检查所有输入的新鲜度 + 有效性
   - 执行 IEC 61508 轨道（Watchdog + 诊断覆盖）
   - 执行 SOTIF 轨道（6 项假设违反检测）
   - Safety Arbitrator 汇聚 → 生成 Safety_AlertMsg（若有新告警）

2. **事件处理线程**（异步）：
   - CheckerVetoNotification 到达 → 更新 veto_histogram + 判断否决率是否突破 20% 阈值
   - ReflexActivationNotification 到达 → 设置 sotif_main_arbitration_paused = true
   - OverrideActiveSignal 到达 → 根据 override_active 触发回切顺序化

### 5.2 IEC 61508 轨道算法详节

#### 5.2.1 Watchdog（模块心跳检测）

```
def watchdog_monitor():
    # 目标：检测 M1/M2/M3/M4/M5/M6/M8 是否存活
    # 策略：订阅各模块的周期消息；若超时未收到则计数
    
    for module in [M1, M2, M3, M4, M5, M6, M8]:
        age_ms = current_time - module.last_input_time
        expected_period_ms = module.expected_period * 1.5  # 允许 1.5× 超时
        
        if age_ms > expected_period_ms:
            module.heartbeat_loss_count += 1
            if module.heartbeat_loss_count >= 3:
                # 三次连续超时
                trigger_alert(IEC61508_FAULT, CRITICAL, 
                             f"Module {module.name} heartbeat loss")
                return MRM_01  # 保守建议：减速
        else:
            module.heartbeat_loss_count = 0  # 重置计数
```

**参数**（v1.1.1 §5.3 + §11.8）：

| 模块 | 正常频率 | 超时阈值 | 容错次数 |
|---|---|---|---|
| M1 | 1 Hz | 1.5 s | 3 次 |
| M2 | 4 Hz | 300 ms | 3 次 |
| M3 | 0.2 Hz | 7.5 s | 2 次 |
| M4 | 2 Hz | 750 ms | 3 次 |
| M5 | 1–2 Hz | 1 s | 3 次 |
| M6 | 2 Hz | 750 ms | 3 次 |
| M8 | 10 Hz | 150 ms | 3 次 |

#### 5.2.2 诊断覆盖（SIL 2 关键输出一致性校验）

```
def sil2_diagnostic_coverage():
    # 目标：验证关键输出的一致性（M2 CPA / M1 ODD 状态）
    # 策略：多源交叉验证 + 范围检查
    
    # === CPA 一致性检查 ===
    # M2 产生 CPA，M7 不重新计算（避免复制代码）；
    # 但通过独立的 range + velocity 估计做 sanity check
    
    def validate_cpa(target):
        range_m = target.range_to_own_m
        own_speed_mps = own_vessel.speed_mps
        target_speed_mps = target.speed_mps
        
        # 极简单法：假设相对速度恒定，推算碰撞时间
        rel_speed = target_speed_mps - own_speed_mps  # 负值 = 接近
        if rel_speed < -0.1:  # 接近
            time_to_collision_s = range_m / abs(rel_speed)
            cpa_from_range = range_m  # 保守估计
            
            # M2 报告的 CPA 应接近此估计（±10%）
            if abs(target.cpa_nm * 1852 - cpa_from_range) > cpa_from_range * 0.1:
                log_diagnostic("CPA inconsistency detected", severity=WARNING)
                increment_iec61508_fault_count()
        
        return target.cpa_nm  # 若通过检查，使用 M2 值
    
    # === ODD 合规性 sanity check ===
    conformance_score = m1_input.conformance_score
    if conformance_score < 0 or conformance_score > 1:
        trigger_alert(IEC61508_FAULT, CRITICAL, "Invalid conformance_score")
        return MRM_02  # 停车
    
    # === COLREGs 规则一致性 ===
    # M6 的 Rule violations 不应与 M2 目标几何矛盾
    if m6_input.rule_violations and m2_input.targets:
        for violation in m6_input.rule_violations:
            target_id = violation.target_id
            target = find_target_by_id(m2_input.targets, target_id)
            if not target:
                log_diagnostic("Target in violation list not found in M2", 
                             severity=WARNING)
                increment_iec61508_fault_count()
```

**诊断覆盖分类**（v1.1.1 §11.4）：

- **Type A（关键输出）**：CPA / TCPA / ODD 合规评分 → DC ≥ 90%（覆盖 90% 可能的失效模式）
- **Type B（辅助输出）**：目标列表 / Rule violations → DC ≥ 70%（覆盖 70%）
- **故障检出率（FMEA 补充）**：见 §11 FMEA 表格

### 5.3 SOTIF 轨道算法详节

#### 5.3.1 6 项假设违反检测

**假设 1：AIS/雷达一致性**

```
def check_ais_radar_consistency():
    # M2 fusion_confidence 反映两源一致性程度
    # M7 进一步做统计检验：位置残差 > 2σ 持续 10 s
    
    if m2_input.fusion_confidence >= 0.8:
        # 融合高置信度，假设验证通过
        return PASS
    
    if m2_input.fusion_confidence < 0.5:
        # 融合低置信度
        log_assumption_violation("AIS/Radar mismatch", 
                               confidence=m2_input.fusion_confidence)
        self.sotif.assumption_violation.ais_radar_mismatch = True
        self.sotif.assumption_violation.range_residual_m = get_residual_estimate()
        
        # 超过 30 s 持续低置信度 → 升级 SOTIF 告警
        if self.sotif.assumption_violation.ais_radar_mismatch_duration > 30:
            trigger_alert(SOTIF_ASSUMPTION_VIOLATION, WARNING,
                         "AIS/Radar mismatch > 30 s")
            return MRM_01  # 减速观察
    
    return PASS
```

**假设 2：目标运动可预测性**

```
def check_motion_predictability():
    # M2 / M5 基于目标当前速度推测轨迹
    # 若预测误差过大（RMS > 50 m/30 s），则目标行为不可预测
    # 原因：目标突然加速 / 转向 / 或 AIS 数据滞后
    
    pred_window_s = 30
    pred_rmse_m = calculate_prediction_rmse(m2_input.targets, pred_window_s)
    
    self.sotif.assumption_violation.pred_rmse_m = pred_rmse_m
    
    if pred_rmse_m > 50.0:  # [HAZID 校准]
        log_assumption_violation("Motion unpredictable", 
                               pred_rmse=pred_rmse_m)
        self.sotif.assumption_violation.motion_unpredictable = True
        
        # 缓解：加大 CPA 安全边距
        safe_cpa_nm = max(target.cpa_nm for target in m2_input.targets) * 1.3
        trigger_alert(SOTIF_ASSUMPTION_VIOLATION, WARNING,
                     f"Motion unpredictable; enlarged CPA threshold = {safe_cpa_nm} nm")
        
        # 若 TCPA < 5 min，触发 MRM
        if min(target.tcpa_sec for target in m2_input.targets) < 300:
            return MRM_02  # 停车（保守）
    
    return PASS
```

**假设 3：感知覆盖充分性**

```
def check_perception_coverage():
    # Multimodal Fusion 的雷达 / LiDAR / 摄像机可能存在盲区
    # 若 360° 覆盖中超过 20% 存在盲区（无目标检测能力），则覆盖不充分
    
    blind_zone_fraction = m2_input.blind_zone_fraction  # M2 / Fusion 计算
    
    self.sotif.assumption_violation.blind_zone_fraction = blind_zone_fraction
    
    if blind_zone_fraction > 0.20:  # 20% [HAZID 校准]
        log_assumption_violation("Coverage insufficient", 
                               blind_zone_pct=blind_zone_fraction * 100)
        self.sotif.assumption_violation.perception_coverage_poor = True
        
        # 缓解：禁止 D4，降至 D3（人工保持注意）
        trigger_alert(SOTIF_ASSUMPTION_VIOLATION, CRITICAL,
                     f"Perception coverage poor ({blind_zone_fraction*100:.1f}% blind)")
        return MRM_01  # 减速 + 降级
    
    return PASS
```

**假设 4：COLREGs 可解析性**

```
def check_colregs_solvability():
    # M6 可能因场景复杂性无法解析 COLREGs
    # 若连续 3 次 processing_success=false，则规则不可解析
    
    if m6_input.processing_success:
        self.sotif.assumption_violation.colregs_fail_count = 0
        return PASS
    
    self.sotif.assumption_violation.colregs_fail_count += 1
    
    if self.sotif.assumption_violation.colregs_fail_count >= 3:
        log_assumption_violation("COLREGs unsolvable", fail_count=3)
        self.sotif.assumption_violation.colregs_unsolvable = True
        
        # 缓解：触发 M7 告警 + 建议降速
        trigger_alert(SOTIF_ASSUMPTION_VIOLATION, CRITICAL,
                     "COLREGs resolution failed 3 times")
        return MRM_01  # 减速 + ROC 告警
    
    return PASS
```

**假设 5：通信链路可用性**

```
def check_comm_link():
    # D3/D4 依赖 ROC 链路，若 RTT > 2 s 或丢包 > 20%，链路降质
    
    rtt_sec = measure_rtt()
    packet_loss_pct = measure_packet_loss()
    
    self.sotif.assumption_violation.rtt_sec = rtt_sec
    self.sotif.assumption_violation.packet_loss_pct = packet_loss_pct
    
    if rtt_sec > 2.0 or packet_loss_pct > 20.0:  # [HAZID 校准]
        log_assumption_violation("Comm degraded", 
                               rtt=rtt_sec, loss_pct=packet_loss_pct)
        self.sotif.assumption_violation.comm_degraded = True
        
        # 缓解：D4 不允许（改为 D3）；TMR 窗口收窄
        if m1_input.auto_level == D4:
            trigger_alert(SOTIF_ASSUMPTION_VIOLATION, CRITICAL,
                         "D4 not allowed; comm degraded")
            return MRM_01  # 降至 D2 + 减速
    
    return PASS
```

**假设 6：Checker 否决率**

```
def check_checker_veto_rate():
    # X-axis Checker 否决 M5 输出的频率
    # 若 > 20% / 100 周期 [HAZID 校准]，则 COLREGs 推理可信度下降
    
    # 更新滑动窗口（circular buffer，100 个槽）
    if checker_veto_received():
        veto = receive_checker_veto_notification()  # RFC-003 消息
        self.checker.veto_window[self.checker.veto_window_start_idx] = 1
        self.checker.veto_histogram[veto.veto_reason_class] += 1
    else:
        self.checker.veto_window[self.checker.veto_window_start_idx] = 0
    
    self.checker.veto_window_start_idx = (self.checker.veto_window_start_idx + 1) % 100
    
    veto_rate = sum(self.checker.veto_window) / 100.0
    
    if veto_rate > 0.20:  # 20% 阈值 [HAZID 校准]
        log_assumption_violation("Checker veto rate high", 
                               rate=veto_rate, 
                               histogram=self.checker.veto_histogram)
        self.sotif.assumption_violation.checker_veto_high = True
        
        # 缓解：升级 SOTIF 告警
        trigger_alert(SOTIF_ASSUMPTION_VIOLATION, WARNING,
                     f"Checker veto rate {veto_rate*100:.1f}%; "
                     f"COLREGs confidence may be low")
        return MRM_01  # 减速观察
    
    return PASS
```

#### 5.3.2 性能退化检测

```
def check_performance_degradation():
    # CPA 趋势恶化：若 CPA 单调递减 > 30 s，危险趋势
    # 原因：目标加速或本船转向不足
    
    if not m2_input.targets:
        return PASS
    
    min_cpa = min(target.cpa_nm for target in m2_input.targets)
    
    # 维护 30 s 历史窗口
    append_to_cpa_history(min_cpa)
    
    if len(self.cpa_history) >= 30:
        cpa_trend_slope = (self.cpa_history[-1] - self.cpa_history[0]) / 30
        
        self.sotif.performance.cpa_trend_slope = cpa_trend_slope
        self.sotif.performance.max_cpa_in_window = max(self.cpa_history)
        
        if cpa_trend_slope < -0.01:  # CPA 每秒递减 > 0.01 nm [HAZID 校准]
            self.sotif.performance.cpa_trend_degrading = True
            
            trigger_alert(SOTIF_PERFORMANCE_DEGRADATION, CRITICAL,
                         f"CPA degrading {cpa_trend_slope:.4f} nm/s")
            
            # 紧急 MRM
            return MRM_02  # 停车
    
    # 多目标聚集
    critical_targets = [t for t in m2_input.targets if t.cpa_nm < 1.0]
    if len(critical_targets) >= 2:
        self.sotif.performance.multiple_targets_nearby = True
        self.sotif.performance.critical_target_count = len(critical_targets)
        
        trigger_alert(SOTIF_PERFORMANCE_DEGRADATION, CRITICAL,
                     f"{len(critical_targets)} targets within 1.0 nm")
        
        return MRM_03  # 紧急转向（多目标时比减速更有效）
    
    return PASS
```

#### 5.3.3 触发条件识别

```
def check_triggering_condition():
    # 极端场景：多条假设同时违反
    
    violation_count = sum([
        self.sotif.assumption_violation.ais_radar_mismatch,
        self.sotif.assumption_violation.motion_unpredictable,
        self.sotif.assumption_violation.perception_coverage_poor,
        self.sotif.assumption_violation.colregs_unsolvable,
        self.sotif.assumption_violation.comm_degraded,
        self.sotif.assumption_violation.checker_veto_high,
    ])
    
    self.sotif.assumption_violation.violation_count = violation_count
    
    if violation_count >= 3:  # 3 个或以上同时违反 [HAZID 校准]
        self.sotif.assumption_violation.extreme_scenario_detected = True
        
        trigger_alert(SOTIF_ASSUMPTION_VIOLATION, EMERGENCY,
                     f"Extreme scenario: {violation_count} assumptions violated simultaneously")
        
        return MRM_02  # 停车（最保守）
    
    return PASS
```

### 5.4 Safety Arbitrator（Alert 汇聚与 MRM 建议）

```
def safety_arbitrator():
    """
    两轨信号汇聚 → 生成 Safety_AlertMsg
    """
    
    # 1. 收集所有 Alert（Track 1 + Track 2）
    alerts = []
    
    # IEC 61508 轨道
    if self.iec61508.heartbeat_loss_count > 0:
        alerts.append({
            'type': IEC61508_FAULT,
            'severity': CRITICAL if sum(self.iec61508.heartbeat_loss_count.values()) >= 2 else WARNING,
            'rationale': f"Watchdog alert: {self.iec61508.heartbeat_loss_count} modules unresponsive",
            'recommended_mrm': MRM_01 if sum(self.iec61508.heartbeat_loss_count.values()) < 2 else MRM_02,
            'confidence': 0.95
        })
    
    if self.iec61508_fault_count > 0:
        alerts.append({
            'type': IEC61508_FAULT,
            'severity': CRITICAL,
            'rationale': f"SIL2 diagnostic failed: {self.iec61508_fault_count} fault",
            'recommended_mrm': MRM_02,
            'confidence': 0.99
        })
    
    # SOTIF 轨道
    if self.sotif.assumption_violation.ais_radar_mismatch:
        alerts.append({
            'type': SOTIF_ASSUMPTION_VIOLATION,
            'severity': WARNING,
            'rationale': f"AIS/Radar inconsistency ({self.sotif.assumption_violation.range_residual_m:.1f} m)",
            'recommended_mrm': MRM_01,
            'confidence': 0.8
        })
    
    if self.sotif.assumption_violation.motion_unpredictable:
        alerts.append({
            'type': SOTIF_ASSUMPTION_VIOLATION,
            'severity': WARNING,
            'rationale': f"Target motion unpredictable (pred_rmse={self.sotif.assumption_violation.pred_rmse_m:.1f} m)",
            'recommended_mrm': MRM_01,
            'confidence': 0.7
        })
    
    # ... 其他 5 项假设类似 ...
    
    if self.sotif.performance.cpa_trend_degrading:
        alerts.append({
            'type': SOTIF_PERFORMANCE_DEGRADATION,
            'severity': CRITICAL,
            'rationale': f"CPA degrading {self.sotif.performance.cpa_trend_slope:.4f} nm/s",
            'recommended_mrm': MRM_02,
            'confidence': 0.95
        })
    
    # 2. 优先级排序 & 汇聚
    if not alerts:
        return None  # 无告警
    
    # 按 severity 排序：EMERGENCY > CRITICAL > WARNING
    alerts.sort(key=lambda a: (
        {EMERGENCY: 0, CRITICAL: 1, WARNING: 2}[a['severity']],
        -a['confidence']
    ))
    
    # 取最高优先级的告警
    primary_alert = alerts[0]
    
    # 汇聚其他告警到 rationale
    other_rationales = [a['rationale'] for a in alerts[1:4]]  # 最多汇聚 3 个
    
    # 3. MRM 建议优先级（需要 30 s 稳定性 + 90% 置信度）
    recommended_mrm = primary_alert['recommended_mrm']
    
    # 检查是否需要升级 MRM
    if primary_alert['severity'] == CRITICAL and primary_alert['recommended_mrm'] == MRM_01:
        if self.sotif.assumption_violation.violation_count >= 2:
            recommended_mrm = MRM_02  # 升级到停车
    
    if primary_alert['severity'] == EMERGENCY:
        recommended_mrm = MRM_02  # 最高级别 → 停车
    
    # 防止振荡：若上一个 MRM 在 30 s 内，不切换
    if (current_time - self.mrm.last_mrm_change_time) < 30:
        if self.mrm.last_recommendation != NONE:
            recommended_mrm = self.mrm.last_recommendation  # 保持不变
            primary_alert['confidence'] *= 0.8  # 降低置信度标记
    
    # 4. 生成 Safety_AlertMsg
    safety_msg = Safety_AlertMsg(
        timestamp=current_time,
        type=primary_alert['type'],
        severity=primary_alert['severity'],
        recommended_mrm=recommended_mrm,
        confidence=primary_alert['confidence'],
        rationale=primary_alert['rationale'] + " | ".join(other_rationales),
        other_alerts_count=len(alerts) - 1
    )
    
    # 5. 更新内部状态
    self.mrm.last_recommendation = recommended_mrm
    self.mrm.last_mrm_change_time = current_time
    
    return safety_msg
```

### 5.5 复杂度分析

**时间复杂度**（每个周期）：

```
Watchdog:                       O(8) = O(1)          // 固定 8 个模块
IEC 61508 diagnostic coverage:  O(n_targets)         // n_targets ≤ 200
SOTIF 假设违反检测:
  · AIS/Radar:                  O(1)
  · Motion predictability:       O(30) = O(1)        // 30 s 窗口
  · Perception coverage:         O(1)
  · COLREGs solvability:         O(1)
  · Comm link:                   O(1)
  · Checker veto:                O(1)（直接查询 histogram）
SOTIF 性能退化检测:             O(n_targets)
Safety Arbitrator:              O(k log k) = O(log 6) = O(1)  // k ≤ 6 条 Alert

Total per cycle:                O(n_targets) ≈ O(200) = O(1)（界限常数）
```

**空间复杂度**：

```
Watchdog state:                 O(8) = O(1)
SOTIF state:                    O(30) = O(1)        // 30 s 历史窗口
Checker veto histogram:         O(|VetoReasonClass|) = O(6) = O(1)
Alert queue:                    O(6) = O(1)

Total:                          O(1)（所有状态变量皆为常数界）
```

**实时性约束**：

- **Watchdog + IEC61508 诊断**：< 50 ms（对齐 M2 4 Hz 周期）
- **SOTIF 假设违反检测**：< 50 ms（预计算 + 直接查询）
- **Safety Arbitrator**：< 10 ms（简单排序 + 逻辑判断）
- **总端到端延迟**：< 100 ms（v1.1.1 §11 输出 SLA）

---

## 6. 时序设计（Timing Design）

### 6.1 周期任务

**主监控线程**（10 Hz 或 4 Hz 对齐 M2 频率）：

```
┌─ 周期 = 100 ms (10 Hz) 或 250 ms (4 Hz) ─┐
│                                           │
├─ T0:     轮询输入队列 (Watchdog)          │
├─ T0+10:  IEC61508 诊断                    │
├─ T0+20:  SOTIF 假设违反检测               │
├─ T0+30:  性能退化检测                     │
├─ T0+40:  Safety Arbitrator 汇聚           │
├─ T0+50:  生成 Safety_AlertMsg（若有）    │
├─ T0+60:  ASDR 记录                        │
├─ T0+70:  SAT_DataMsg 更新                 │
├─ T0+80:  发布所有消息                     │
└─ T0+100: 周期完成                         │
```

**事件处理线程**（异步，优先级高于主线程）：

```
if CheckerVetoNotification arrived:
    → 更新 veto_histogram（立即）
    → 判断是否超过 20% 阈值（立即）
    → 若超过，发送 SOTIF 告警（< 50 ms）

if ReflexActivationNotification arrived:
    → 设置 sotif_main_arbitration_paused = true（立即）
    → 发送回应信号到 M1（< 50 ms）

if OverrideActiveSignal arrived:
    if override_active = true:
        → 设置 OVERRIDDEN 模式
        → 启动降级监测线程
    if override_active = false:
        → 启动回切顺序化（§6.3 详细）
```

### 6.2 延迟预算（Delay Budget）

```
输入事件 → Alert 输出的完整链路：

┌─────────────────────────────────────┐
│ M2 / M6 新 msg 到达                 │  T0
│ (received_stamp)                    │
├─────────────────────────────────────┤
│ M7 轮询输入队列                     │  T0 + (0–100) ms  [主线程周期抖动]
│ 复制 msg 快照 + 时间戳              │
├─────────────────────────────────────┤
│ 执行 Watchdog                       │  T0 + ~10 ms
│ 执行 IEC61508 诊断                  │  T0 + ~20 ms
│ 执行 SOTIF 检测                     │  T0 + ~50 ms
├─────────────────────────────────────┤
│ Safety Arbitrator                   │  T0 + ~60 ms
│ 生成 Safety_AlertMsg                │
├─────────────────────────────────────┤
│ 发布到 M1（ROS2 DDS）              │  T0 + ~80 ms
│ M1 接收 + 队列延迟                 │  T0 + (80–150) ms  [M1 处理延迟]
├─────────────────────────────────────┤
│ M1 仲裁 + 发送 MRM 命令到 M5       │  T0 + ~200 ms 从 M2 角度
│ 或 M1 发送 Alert 到 ASDR            │
└─────────────────────────────────────┘

SLA: M7 Alert 生成 ≤ 100 ms（相对 M2 输入时间戳）✓
```

### 6.3 回切顺序化（Detailed Sequencing — v1.1.1 §11.9.2 F-NEW-006）

当 `OverrideActiveSignal { override_active=false }` 到达时，执行严格的回切顺序：

```
T0:        Hardware Override Arbiter → M1: OverrideActiveSignal { override_active=false }

T0+0 ms:   M1 主线程轮询 override_active=false 信号
           → 设置 M1.mode = REVERTING_FROM_OVERRIDE

T0+10 ms:  M7 收到回切信号（通过 M1 或直接）
           → 启动 SOTIF 主仲裁线程
           → 重置假设违反状态（清空历史窗口）
           → 开始周期监控

T0+100 ms: M7 运行 5–10 个完整周期后，对内部状态进行一致性检查
           → 若无异常 → 生成 "M7_READY" 信号到 M1
           → 若有异常（如输入不一致）→ 延迟至 T0+200 ms 重试

T0+110 ms: M1 收到 M7_READY
           → 向 M5 发送 "M5_RESUME" 信号

T0+110 ms: M5 接收 M5_RESUME
           → 读取当前 M2 + M4 + M6 状态（新快照）
           → 重置 Mid-MPC 积分项：
             * y_integral = 0
             * psi_integral = 0
             * r_integral = 0
           → 重置 BC-MPC 积分项（同上）
           → 设置 M5.mode = NORMAL

T0+120 ms: M5 首次输出新 AvoidancePlan（status = NORMAL）
           → 该输出通过 M7 SOTIF 监控（M7 已在 T0+110 ms 启动）
           → M5 输出 ≤ 100 ms（相对 Arbitrator 启动）后被 M7 验证 ✓

T0+150 ms: ASDR 记录完整的回切顺序时间戳：
           { 
             event: "override_released",
             t_override_inactive: T0,
             t_m7_main_start: T0+10,
             t_m7_ready: T0+100,
             t_m5_resume_received: T0+110,
             t_m5_first_output: T0+120,
             t_asdr_log: T0+150
           }

超时保护：
  如果 M7 在 T0+100 ms 内未发出 M7_READY，M1 自动切换到 D2 + 触发 MRM-01
  如果 M5 在 T0+150 ms 内无新输出，M1 切换到 D2 + 触发 MRM-01
```

**关键设计原则**（v1.1.1 §11.9.2 — F-NEW-006 关闭）：

1. **M7 先于 M5 启动**（间隔 ≥ 100 ms）：保证 M5 输出时 M7 SOTIF 监控已激活
2. **积分项重置**：防止历史误差累积导致回切瞬态不稳定
3. **时间戳持久化**：所有关键时间点记录到 ASDR（可审计）
4. **超时保护**：若回切顺序异常，系统自动降级至最安全状态（D2 + MRM-01）

---

## 7. 降级与容错（Degradation & Fault Tolerance）

### 7.1 降级路径

M7 支持三级降级：

| 降级状态 | 触发条件 | M7 行为 | 系统级响应 |
|---|---|---|---|
| **NORMAL** | 所有输入 OK，双轨无告警 | 周期监控 10 Hz，判断是否触发 MRM | 无影响，系统正常 |
| **DEGRADED** | 单个输入超时 OR 单个假设违反 OR Watchdog 1–2 模块失联 | 保留 SOTIF 监控；推荐 MRM-01（减速）；进入 DEGRADED 模式 | M1 可保持 D3 / 降至 D2（取决于严重性） |
| **CRITICAL** | 多个假设同时违反（≥ 3 项）OR 多个模块失联（≥ 3 个）OR CPA 急剧恶化 | 触发 CRITICAL 级 Alert；推荐 MRM-02（停车）OR MRM-03（紧急转向）| M1 必须切换到 D2；触发 MRC 准备 |
| **FAILED** | M7 自身死亡（心跳丢失 > 3 周期）OR 无法恢复的内部错误 | Fail-Safe → 强制向 M1 发送 MRM-01；之后停止所有监控 | 系统自动降至 D2 + MRM-01；人工接管 |

**降级触发条件详表**（[HAZID 校准]）：

```
NORMAL → DEGRADED:
  ├─ 任一输入延迟 ∈ [100 ms, 500 ms)
  ├─ 单个假设违反（AIS/Radar mismatch）
  ├─ 单个模块心跳丢失（1–2 次）
  └─ Checker 否决率 ∈ [10%, 20%)

DEGRADED → CRITICAL:
  ├─ 多个假设同时违反（violation_count ≥ 2）
  ├─ 多个模块心跳丢失（≥ 3 个）
  ├─ 输入延迟 ≥ 500 ms
  └─ CPA 趋势 slope < -0.01 nm/s

CRITICAL → FAILED:
  ├─ M7 自身心跳丢失
  ├─ 内存不足 / 算法超时
  └─ 不可恢复的计算错误
```

**从 DEGRADED/CRITICAL 恢复**：

```
def attempt_recovery():
    # 恢复条件：问题修复 + 等待确认周期
    
    if m7.mode == DEGRADED:
        # 需要连续 5 s（50 周期 @ 10 Hz）无新告警
        if time_since_last_alert > 5:
            m7.mode = NORMAL
            trigger_alert(INFO, "M7 recovered to NORMAL mode")
    
    elif m7.mode == CRITICAL:
        # 需要连续 10 s 无新 Alert，且输入恢复正常
        if time_since_last_alert > 10 and inputs_all_fresh():
            m7.mode = DEGRADED
            # 继续观察，不直接回到 NORMAL
    
    elif m7.mode == FAILED:
        # 无自动恢复；需人工重启 M7
        return FAILED
```

### 7.2 FMEA（故障模式分析）

**M7 自身失效 FMEA**：

| 失效模式 | 原因 | 后果 | 严重度 | 检出 | 缓解 |
|---|---|---|---|---|---|
| **Watchdog 失效** | 输入总线故障 / M1–M8 全部失联 | 无法检测模块死亡 | Catastrophic | 难 | 多种输入源（心跳来自 5+ 不同消息） + 交叉检验 |
| **假设违反检测逻辑错误** | 算法 bug（如 CPA 阈值倒置） | 错误阈值导致虚警或漏警 | Major | 中 | 形式化验证 + HIL 模拟器覆盖 ≥ 99% 场景 |
| **Safety Arbitrator 汇聚错误** | 多条 Alert 时优先级排序逻辑失效 | 推荐错误的 MRM | Major | 高 | 单元测试覆盖所有优先级组合（6×5=30 组合）|
| **MRM 振荡** | 阈值定义过敏感，导致 30 s 内切换 MRM | 频繁切换命令，船舶不稳定 | Major | 高 | 30 s 防振荡锁定 + 置信度升级门槛 (> 0.9) |
| **回切失败** | M7_READY 信号丢失或超时 | 人工接管无法顺利回切 | Critical | 中 | 超时保护（T0+100 ms 未 READY → 自动 D2 + MRM-01）|
| **Fail-Silent（禁止）** | M7 内部异常但继续输出虚假 "正常" | 系统决策基于错误的安全保证 | Catastrophic | 难 | Watchdog（外部 X-axis Checker 监控 M7 心跳）+ Health flag |
| **Checker veto 消息丢失** | ROS2 DDS 消息掉帧 | Checker 否决率低估，假设违反未升级 | Major | 中 | 事件聚合窗口宽度加大（150 周期 vs 100）+ 超时告警 |

**降级路径验证**（与 §11 M7 一致性）：

- M7 Watchdog 失效 → **Fail-Safe**（强制 MRM-01）→ 系统自动降级 D2 ✓
- M7 假设违反检测误触发 → M7 DEGRADED → 人工观察 → 若确认误触，M1 覆盖可逃逸 ✓
- M7 回切失败 → 超时保护 → 自动 D2 + MRM-01 ✓

### 7.3 心跳与监控

**M7 自身心跳**（被动监控 — 由 X-axis Checker 监控）：

```
M7 → ASDR: 
  - 每 100 ms 发送 "M7_HEARTBEAT" 消息（含 mode + health status）
  - X-axis Checker 订阅此心跳
  - 若丢失 > 3 次（> 300 ms），X-axis 向 M1 发送 Checker VETO：
    { veto_reason_class: TIMEOUT, detail: "M7 heartbeat lost" }
  - M1 接收此 VETO → 触发 MRC 准备（M7 失能）
```

**M7 监控的心跳列表**（Watchdog）：

| 模块 | 预期来源 | 频率 | 超时 | 容错次数 |
|---|---|---|---|---|
| M1 | ODD_StateMsg | 1 Hz | 1.5 s | 3 次 |
| M2 | World_StateMsg | 4 Hz | 300 ms | 3 次 |
| M3 | (VoyageTask subscribe) | 0.2 Hz | 7.5 s | 2 次 |
| M4 | (Behavior action) | 2 Hz | 750 ms | 3 次 |
| M5 | AvoidancePlanMsg | 1–2 Hz | 1 s | 3 次 |
| M6 | COLREGs_ConstraintMsg | 2 Hz | 750 ms | 3 次 |
| M8 | (UI state topic) | 10 Hz | 150 ms | 3 次 |

**接管期间降级监测线程**（v1.1.1 §11.9.1 — F-NEW-005）：

当 Hardware Override 激活时，M7 暂停主仲裁线程，但保留 4 项降级监测：

```
降级监测线程（接管期间 @ 10 Hz）：

┌─ 监测 1：通信链路 ─┐
│ 若 RTT > 2 s 或丢包 > 50% → 实时显示红色告警
├─ 监测 2：传感器降质 ─┤
│ 若 Fusion status = DEGRADED → 显示黄色告警
├─ 监测 3：新威胁出现 ─┤
│ 若新 target 出现 CPA < 0.5 nm → 显示红色 "新威胁"
└─ 监测 4：M7 自身异常 ──┘
  若 M7 心跳丢失 → 显示 "安全监督失效" 高优先级红色
  
所有告警通过 M8 实时显示（< 100 ms 时延）
法律依据：IMO MASS Code 要求"安全相关信息须实时呈现"
```

---

## 8. 与其他模块协作（Collaboration）

### 8.1 与上下游模块的握手

**M1 ↔ M7 握手**：

```
M1 → M7:
  ├─ ODD_StateMsg（1 Hz）：当前 ODD 子域 / 自主等级 / 健康状态
  └─ [接管时] OverrideActiveSignal：override_active 状态

M7 → M1:
  ├─ Safety_AlertMsg（事件 + 周期）：建议 MRM + severity
  └─ M7_READY（接管回切时）：回切顺序化信号

握手协议：
  - M1 订阅 Safety_AlertMsg；若 type=EMERGENCY，立即触发 MRC
  - M1 订阅 CheckerVetoNotification（转发自 X-axis Checker）
  - M1 在 MRC 期间不向 M7 发送 ODD 更新（保持快照）
  - M7 在回切时向 M1 发 M7_READY，M1 才启动 M5
```

**M2 ↔ M7 握手**：

```
M2 → M7:
  ├─ World_StateMsg（4 Hz）：targets[]、CPA、TCPA、融合置信度
  └─ 融合健康状态（FULL / DEGRADED / CRITICAL）

M7 → M2:
  └─ 无直接反向通信（M7 是被动监控）

握手协议：
  - M7 不重新计算 CPA（避免代码复用）；信任 M2 值 + 交叉检验
  - M2 status = DEGRADED 时，M7 自动采用保守 CPA_safe = 0.5 nm（F-P1-D3-003）
  - M7 若检测到 CPA 不一致，记录 diagnostic fault（不直接告警，防止共振）
```

**M5 ↔ M7 握手**：

```
M5 → M7:
  ├─ AvoidancePlanMsg（1–2 Hz）：plan_status / confidence / execution_confidence
  └─ 该消息用于 M7 监控 M5 的"健康度"（非用于决策）

M7 → M5:
  └─ 无直接通信；M7 通过 M1 下发 MRM 命令（M1 → M5）

握手协议：
  - M7 不会否决或修改 M5 输出（保持 Doer-Checker 单向关系）
  - M5 plan_status = TIMEOUT → M7 记录 SOTIF 告警（M5 规划缓冲溢出）
  - 回切时 M7_READY → M1 → M5_RESUME 信号（严格顺序）
```

**M6 ↔ M7 握手**：

```
M6 → M7:
  ├─ COLREGs_ConstraintMsg（2 Hz）：processing_success / rule_violations / confidence
  └─ 用于 M7 检测 COLREGs 可解析性

M7 → M6:
  └─ 无直接反向通信

握手协议：
  - M7 不解析 rule_violations 内容（避免重复规则推理）
  - 若 processing_success 连续 false 3 次 → M7 触发"COLREGs 不可解析"告警
  - M6 confidence < 0.5 → M7 自动降低对 Rule 的信任度
```

**X-axis Checker ↔ M7 握手**（RFC-003）：

```
X-axis → M7:
  └─ CheckerVetoNotification（事件）：
       {veto_reason_class: enum, veto_reason_detail: string, ...}

M7 → X-axis:
  └─ 无直接通信（M7 不会反向指挥 Checker）

握手协议：
  - M7 仅按 veto_reason_class enum 分类做统计（不解析 detail）
  - 否决率 = sum(veto_window) / 100 > 20% [HAZID 校准] → SOTIF 告警升级
  - veto_reason_detail 仅供 ASDR 记录，M7 不处理
  - 多层 Checker（L2/L3/L4/L5）否决在 M7 端：仅统计 L3 Checker 否决率
    （其他层由系统层 ASDR 单独聚合）
```

**Reflex Arc ↔ M7 握手**（RFC-005）：

```
Reflex Arc → M7:
  └─ ReflexActivationNotification（事件）：
       {activation_time, reason, l3_freeze_required}

M7 → Reflex Arc:
  └─ 无直接通信

握手协议：
  - M7 接收 ReflexActivationNotification 后设置 sotif_main_arbitration_paused = true
  - M7 保留降级监测线程（继续显示告警）
  - 若 Reflex 持续激活 > 30 s，M7 向 M1 发送"Reflex 可能卡住"告警
```

### 8.2 SAT-1/2/3 输出（与 M8 协作）

**SAT_DataMsg 结构**（M7 → M8 @ 10 Hz）：

```
message SAT_DataMsg {
    timestamp    stamp;
    
    // === SAT-1 当前态势 ===
    struct SAT_1_State {
        string      current_auto_level;      // D2/D3/D4
        string      current_odd_zone;        // A/B/C/D
        float       conformance_score;       // 0–1
        uint32_t    alert_count;             // 当前活跃 Alert 数
        
        struct Target {
            uint32_t    track_id;
            float       cpa_nm;
            float       tcpa_sec;
            float       bearing_deg;
        };
        Target      critical_targets[3];     // Top-3 by threat
    } sat_1;
    
    // === SAT-2 推理层（按需展开）===
    struct SAT_2_Reasoning {
        bool        expanded;                // 标志是否需要展开显示
        string      assumption_status[6];    // 6 项假设的 OK/VIOLATED 状态
        string      current_mrm_rationale;   // 若推荐 MRM，展示理由
        float       performance_trend;       // CPA 趋势 (nm/s)
    } sat_2;
    
    // === SAT-3 预测（< 30 s 时自动加粗）===
    struct SAT_3_Forecast {
        float       alert_eta_seconds;       // 预计多久后触发下一个 Alert
        float       confidence_trend;        // 系统可信度趋势（+/−/→）
        string      critical_event;          // "None" / "Checker veto high" / "CPA degrading"
    } sat_3;
}
```

**M8 自适应触发规则**（v1.1.1 §12.2 + F-NEW-005）：

```
SAT-1: 全时展示 @ 10 Hz（持续刷新）

SAT-2:
  ├─ COLREGs 冲突检测 → 展开 rule_violations + 推理链
  ├─ M7 SOTIF 告警激活 → 展开假设违反诊断
  ├─ M7 IEC61508 故障 → 展开故障诊断链
  └─ 操作员显式请求（UI 按钮）→ 展开所有推理

SAT-3:
  ├─ TDL < 30 s 时 → 自动加粗 + 加大字体推送
  ├─ alert_eta_seconds < 5 s 时 → 红色 + 蜂鸣
  └─ 操作员请求接管时 → 持续推送（预计接管时窗）
```

### 8.3 ASDR 决策追溯日志格式

**每条 Safety_AlertMsg 的 ASDR 记录**：

```json
{
  "event_type": "M7_SAFETY_ALERT",
  "timestamp": "2026-05-05T12:34:56.789Z",
  "alert_type": "SOTIF_ASSUMPTION_VIOLATION",
  "severity": "CRITICAL",
  "recommended_mrm": "MRM-02",
  "confidence": 0.95,
  
  "input_snapshot": {
    "m1_ood_state": { "auto_level": "D3", "ood_zone": "A", ... },
    "m2_world_state": { "targets_count": 2, "min_cpa_nm": 0.45, ... },
    "m6_colregs_state": { "processing_success": true, ... }
  },
  
  "m7_internal_state": {
    "sotif_violations": [
      { "type": "motion_unpredictable", "pred_rmse_m": 72.5 },
      { "type": "multiple_targets_nearby", "target_count": 2 }
    ],
    "performance_degradation": {
      "cpa_trend_slope": -0.015,
      "time_window_s": 30
    }
  },
  
  "arbitration_details": {
    "track1_iec61508_faults": 0,
    "track2_sotif_violations": 2,
    "other_alerts_suppressed": 0,
    "final_mrm_recommendation": "MRM-02",
    "30s_stability_check": true
  },
  
  "rationale": "Motion unpredictable (pred_rmse=72.5 m) + 2 targets within 1.0 nm → Stop ship",
  
  "execution_status": "Sent to M1 for arbitration"
}
```

**Checker veto 统计聚合记录**（每 100 周期）：

```json
{
  "event_type": "M7_CHECKER_VETO_WINDOW_SUMMARY",
  "timestamp": "2026-05-05T12:35:10.000Z",
  "window_duration_s": 10,
  "veto_histogram": {
    "COLREGS_VIOLATION": 15,
    "CPA_BELOW_THRESHOLD": 3,
    "ENC_CONSTRAINT": 1,
    "ACTUATOR_LIMIT": 0,
    "TIMEOUT": 1,
    "OTHER": 0
  },
  "veto_rate": 0.20,
  "threshold_exceeded": false,
  "remarks": "Exactly at threshold (20%); marginal case for SOTIF alert upgrade"
}
```

---

## 9. 测试策略（Test Strategy）

### 9.1 单元测试

**Watchdog 单元测试**：

```gherkin
Scenario: M2 heart beat loss
  Given M2 is sending World_StateMsg @ 4 Hz
  When M2 messages stop for 500 ms (2 consecutive missed periods)
  Then M7 should increment m2_heartbeat_loss_count
  And M7 should NOT trigger alert yet (< 3 times)
  
  When M2 messages stop for 750 ms (3 consecutive missed periods)
  Then M7 should trigger IEC61508_FAULT alert with severity=CRITICAL
  And recommended_mrm should be MRM-02 (conservative)

Scenario: Watchdog recovery
  Given M7 is in heartbeat loss state for M2
  When M2 resumes sending messages
  Then M7 should reset m2_heartbeat_loss_count to 0 within 1 cycle
  And alert level should NOT immediately drop (wait 5 s confirmation)
```

**SOTIF 假设违反检测单元测试**：

```gherkin
Scenario: AIS/Radar mismatch detection
  Given m2_input.fusion_confidence = 0.45 (low confidence)
  When this condition persists for 31 seconds
  Then M7 should set assumption_violation.ais_radar_mismatch = true
  And should trigger SOTIF_ASSUMPTION_VIOLATION alert
  And recommended_mrm should be MRM-01 (speed reduction)

Scenario: Motion unpredictability
  Given M2 targets with prediction RMS = 62 m (exceeds 50 m threshold)
  When prediction window = 30 s
  Then M7 should detect motion_unpredictable
  And should recommend CPA safe margin × 1.3
  
  If TCPA of any target < 300 s
  Then recommended_mrm should upgrade to MRM-02

Scenario: COLREGs solvability (3 consecutive failures)
  Given M6 processing_success = false
  When this repeats 3 times
  Then M7 should trigger "COLREGs unsolvable" alert
  And recommended_mrm = MRM-01
  And M1 should receive notification to reduce speed + alert ROC
```

**Safety Arbitrator 单元测试**：

```gherkin
Scenario: Alert priority ranking
  Given 3 simultaneous alerts:
    - IEC61508_FAULT (severity=CRITICAL, confidence=0.99)
    - SOTIF_ASSUMPTION_VIOLATION (severity=WARNING, confidence=0.8)
    - SOTIF_PERFORMANCE_DEGRADATION (severity=CRITICAL, confidence=0.85)
  When arbitration happens
  Then primary_alert should be IEC61508_FAULT (highest severity + confidence)
  And other_alerts should be aggregated in rationale

Scenario: MRM recommendation stability (30 s lock)
  Given recommended_mrm = MRM-01
  When new alert would recommend MRM-02 at T0+15 s
  Then M7 should NOT switch; keep MRM-01
  And set confidence *= 0.8 (degraded confidence marker)
  
  When T0+30 s passed
  Then M7 can switch to MRM-02 if new alert still active
```

### 9.2 模块集成测试

**M7 + M2 集成**：

```
Test: M2 CPA 输入验证 + M7 SOTIF 响应
  1. M2 输出 target CPA = 0.3 nm（危险）
  2. M7 应立即检测 < 1.0 nm 的危险目标
  3. M7 应触发 SOTIF 告警
  4. 若多于 1 个危险目标，推荐 MRM-03（转向）而非 MRM-01
```

**M7 + M6 集成**：

```
Test: COLREGs 规则冲突 + M7 SOTIF 响应
  1. M6 输出 processing_success = false
  2. 重复 3 次
  3. M7 应触发"COLREGs 不可解析"告警
  4. M1 应降速 + 告知 ROC
```

**M7 + X-axis Checker 集成**（RFC-003）：

```
Test: Checker veto 统计聚合
  1. Reflex Arc 期间，X-axis Checker 频繁否决 M5 输出
  2. 在 100 周期窗口内，COLREGS_VIOLATION 类否决 ≥ 20
  3. M7 应计算 veto_rate = 20/100 = 0.20，触发 "Checker veto rate at threshold" 告警
  4. M7 应推荐 MRM-01（速度降低以配合 Checker 决策）
```

### 9.3 HIL 测试场景

**场景 1：SOTIF 假设违反 — AIS/雷达不一致**

```
场景设置：
  • 自动船（M5）巡航 ODD-A（开阔水域）
  • 目标船突然关闭 AIS（但仍在雷达范围内）
  • Multimodal Fusion 融合置信度快速下降
  
预期结果：
  ✓ M7 @ T0+20 s 检测 fusion_confidence < 0.5
  ✓ M7 @ T0+50 s 触发 "AIS/Radar mismatch" 告警
  ✓ M7 推荐 MRM-01
  ✓ M1 应降速到 4 kn
  ✓ M8 显示黄色"融合置信度下降"
  
时延验证：
  M2 input (fusion_confidence drop) → M7 Alert ≤ 50 ms ✓
```

**场景 2：IEC 61508 故障 — M2 心跳丢失**

```
场景设置：
  • Multimodal Fusion 发生 segfault，M2 停止发送 World_StateMsg
  • M7 失去目标信息
  
预期结果：
  ✓ M7 @ T0+1 s（3 × 330 ms）检测心跳丢失
  ✓ M7 触发 IEC61508_FAULT alert (severity=CRITICAL)
  ✓ M7 推荐 MRM-02（停船）
  ✓ M1 应立即触发 MRC 准备
  
验证：
  MRM 生成 ≤ 100 ms from M2 timeout ✓
```

**场景 3：Reflex Arc 协作**

```
场景设置：
  • 自动船以 18 kn 巡航
  • 障碍物（浮标）突然出现，距离 <100 m
  • Reflex Arc 触发 EmergencyCommand
  
预期结果：
  ✓ Reflex Arc → M1: ReflexActivationNotification
  ✓ M7 @ T0+10 ms 接收信号，设置 sotif_main_arbitration_paused = true
  ✓ M7 保留降级监测线程（仍显示告警）
  ✓ 人工接管后，M7 @ T0+100 ms 发送 M7_READY
  ✓ M1 @ T0+110 ms 向 M5 发送 M5_RESUME
  ✓ M5 @ T0+120 ms 输出新的 AvoidancePlan
  
时延链验证：
  Obstacle detected → Reflex triggered ≤ 500 ms ✓
  M7 recovery →回切完成 ≤ 200 ms ✓
```

**场景 4：接管回切顺序化**（v1.1.1 §11.9.2 — F-NEW-006）

```
场景设置：
  • ROC 操作员手动接管（override_active=true）
  • 检验系统在接管期间的行为
  • 操作员完成接管，释放控制（override_active=false）
  
预期结果序列：
  T0:        override_active=false 信号到达 M1
  T0+10 ms:  M7 SOTIF 主线程重启
  T0+100 ms: M7 发送 M7_READY
  T0+110 ms: M1 发送 M5_RESUME
  T0+120 ms: M5 输出新 AvoidancePlan（status=NORMAL）
  T0+150 ms: ASDR 记录回切完成
  
严格验证：
  ✓ M7 → M5 启动间隔 ≥ 100 ms
  ✓ M5 积分项重置（速度 / 航向 / ROT 积分全部 = 0）
  ✓ 回切期间无"安全监控真空"（M7 降级线程持续运行）
  ✓ ASDR 时间戳一致（可复现）
```

### 9.4 关键 KPI

| KPI | 初始目标 | 验证方法 |
|---|---|---|
| **Alert 生成延迟** | ≤ 100 ms（相对 M2 输入 stamp） | HIL 场景 1–4 延迟测量 |
| **假设违反检出率** | ≥ 99.5% | 100 场景 × 6 假设 = 600 测试用例 |
| **MRM 振荡频率** | < 5 次/hour（巡航 1 小时） | 长时 HIL 模拟（仿真 10 h 船海况） |
| **回切顺序准确率** | 100%（时间戳精度 ± 10 ms） | 100 次接管回切测试 |
| **Checker veto 统计精度** | ≥ 99%（histogram 与 X-axis 一致） | 交叉验证 ASDR log |
| **Watchdog 检出率** | 100% 检出 ≥ 3 个模块失联 | 9 个模块 × 3 = 27 故障场景 |
| **降级恢复时间** | ≤ 5 s（DEGRADED → NORMAL） | 人工恢复场景 |

---

## 10. 实现约束（Implementation Constraints）

### 10.1 编程语言 / 框架

- **主体实现**：C++17（实时性 + 形式化验证支持）
- **配置 / 参数文件**：YAML（Capability Manifest 同格式）
- **ROS2 DDS**：Humble 或更新版本（用于消息总线）
- **测试框架**：Google Test (gtest) + Gazebo 仿真

### 10.2 实时性约束

- **周期**：主监控线程 4–10 Hz（对齐 M2 频率，4 Hz 推荐）
- **最大单周期执行时间**：50 ms（保留 50 ms 冗余）
- **优先级**：Event handler > Main loop（事件处理最高优先级）
- **栈大小**：预估 < 2 MB（M7_State 常数，无动态申请）
- **禁止操作**：malloc / new（在周期内）/ 系统调用（file I/O）/ 锁等待（使用 lock-free queue）

### 10.3 SIL / DAL 等级要求

**v1.1.1 §11.4 SIL 2 要求**：

- **单通道 HFT=0**：
  - 诊断覆盖 DC ≥ 90%
  - Safe Failure Fraction SFF ≥ 60%（Type A 元件）/ ≥ 70%（Type B 元件）
  - 形式化验证 → Safety Arbitrator 的优先级排序逻辑
  - Watchdog 超时阈值的数学证明

- **推荐 HFT=1 双通道**（关键路径）：
  - Watchdog（两份独立实现）+ voting
  - SOTIF 假设违反检测（两份独立实现）+ majority vote
  - SFF 要求降至 ≥ 90%

**DO-178C 合规性**：

- 代码可追溯性（每行代码对应需求编号）
- 代码评审（两人原则 + 一人独立验证）
- 单元测试代码覆盖 ≥ 95%（MCDC）
- 不允许的构造：goto / setjmp / 动态内存分配 / 递归（无界）

**IEC 61508 补充**：

- FMEA 与 §7.2 FMEA 表一致
- FTA（故障树分析）指向 Watchdog + SIL2_Diagnostic 两条路径
- 失效率数据（MTBF）来自标准部件库或可信测试

### 10.4 第三方库约束（避免共享路径）

**禁止列表**（与 M2/M5/M6 共享代码）：

- ❌ **CPA 计算库**：M2 已实现，M7 不重复；若需交叉验证，用独立的极简算法（差分法）
- ❌ **COLREGs 推理库**：M6 已实现，M7 不解析；仅对 enum 做计数
- ❌ **轨迹预测库**：M5 已实现，M7 不用于决策；仅计算预测误差统计量
- ❌ **Multimodal Fusion 库**：M2 已集成，M7 不调用；仅消费输出的 confidence 字段

**允许列表**（安全、数学库）：

- ✅ **Eigen**（线性代数）：矩阵运算（如不使用，直接数值计算）
- ✅ **spdlog**（日志）：ASDR 记录
- ✅ **Boost.circular_buffer**（滑动窗口）：veto histogram
- ✅ **ROS2 msg 库**：消息定义（标准约定）

**代码独立性验证**（CI 自动检查）：

```bash
# 禁止导入 M2/M5/M6 的内部头文件
grep -r "#include.*[mM]2\|[mM]5\|[mM]6.*\.hpp" src/m7/ && exit 1

# 允许仅包含 msg / srv 定义
grep -r "#include.*msgs\|services" src/m7/ || true
```

---

## 11. 决策依据（References）

### 关键章节引用（v1.1.1 架构报告）

- **§2.5 决策四（Doer-Checker 双轨）**：M7 Checker 独立性约束
- **§11 M7 Safety Supervisor**：全章基础
  - §11.1 决策原因
  - §11.2 双轨监督架构
  - §11.3 假设违反检测清单（6 项）
  - §11.4 SIL 分配建议
  - §11.6 MRM 命令集（v1.1.1 修订，F-NEW-001）
  - §11.7 与 X-axis Checker 协作（v1.1 新增，F-NEW-002）
  - §11.8 M7 自身降级（v1.1 新增）
  - §11.9 接管期间行为（v1.1.1 修订，F-NEW-005/F-NEW-006）
  - §11.9.2 回切顺序化（v1.1.1 新增）
- **§12.2 SAT 三层透明性 + 自适应触发**（F-P1-D1-010 修订）
- **§15.1 Safety_AlertMsg / ASDR_RecordMsg / SAT_DataMsg IDL**
- **§15.2 接口契约总表**（M7 输入/输出）

### RFC 基线

- **RFC-003 M7↔X-axis Checker 协调协议**（CheckerVetoNotification enum 化 — F-NEW-002）
- **RFC-005 Reflex Arc spec 量化**（ReflexActivationNotification + 触发阈值）

### HAZID 工作包

- **02-mrm-parameters.md**：MRM-01/02/03/04 参数初始值 + 校准方法（F-NEW-001）
- **03-sotif-thresholds.md**：6 项假设违反阈值（[HAZID 校准] 标注）

### 学术与工业参考

| ID | 来源 | 内容 | 应用 |
|---|---|---|---|
| [R4] | Veitch et al. (2024) | TMR ≥ 60 s 实证；SAT 透明度悖论 | §3.4 / §12 |
| [R5] | IEC 61508-3/2 | SIL 分配 / HFT / SFF / DC 矩阵 | §7.2 SIL 2 |
| [R6] | ISO 21448:2022 SOTIF | 功能不足（Functional Insufficiency）| §1 职责 / §5.3 |
| [R7] | Yasukawa & Yoshimura 2015 | MMG 4-DOF 操纵模型 | §5.4 MRM-01 工程依据 |
| [R9] | DNV-CG-0264 (2025.01) | AROS 认证；Checker 独立性 §9.3 | §7.1 FMEA / §10.3 SIL |
| [R12] | Jackson 2021 MIT CSAIL | Certified Control 双轨框架 | §2.5 Doer-Checker 理论 |
| [R15] | MUNIN MRC 设计 | MRC 兜底机动（抛锚）| §5.4 MRM-04 工程依据 |
| [R18] | COLREGs Rule 8 "大幅转向" | ±60° 合规性论证 | §5.4 MRM-03 工程依据 |

---

## 12. 修订记录

| 版本 | 日期 | 修订人 | 变更摘要 |
|---|---|---|---|
| v1.0 (草稿) | 2026-05-05 | Claude L3 architect | 首版详细设计；基于 v1.1.1 + RFC-003 + RFC-005 |
| — | — | — | — |

---

## 附录 A：状态机 Mermaid 源码

（见 §4.2 完整 Mermaid 图）

## 附录 B：假设违反检测与 HAZID 校准映射

```
M7 假设违反 ←→ HAZID 工作包 03-sotif-thresholds.md 映射：

假设 1 (AIS/Radar 一致性):
  融合置信度 < 0.5 阈值 ← HAZID-SOTIF-001

假设 2 (运动可预测性):
  预测 RMSE > 50 m / 30 s ← HAZID-SOTIF-002

假设 3 (感知覆盖):
  盲区 > 20% of 360° ← HAZID-SOTIF-003

假设 4 (COLREGs 可解析):
  连续 3 次 processing_success=false ← HAZID-SOTIF-004

假设 5 (通信可用性):
  RTT > 2 s 或丢包 > 20% ← HAZID-SOTIF-005

假设 6 (Checker 否决率):
  > 20% / 100 周期 ← RFC-003 + HAZID-SOTIF-006

所有 [HAZID 校准] 标注的参数须在 FCB HAZID 完成后回填。
```

## 附录 C：MRM 命令集与 §11.6 一致性

M7 §5.4 MRM 建议逻辑与 v1.1.1 §11.6 表格的映射：

```
Safety_AlertMsg.recommended_mrm = {
  IEC61508_FAULT + heartbeat loss (1–2 模块) → MRM-01 ✓ (§11.6)
  IEC61508_FAULT + heartbeat loss (≥ 3 模块) → MRM-02 ✓
  
  SOTIF 单项假设违反 (1 项) → MRM-01 ✓
  SOTIF 多项假设同时违反 (≥ 2 项) → MRM-02 ✓
  
  PERF_DEGRADATION (CPA 趋势恶化) → MRM-02 ✓
  PERF_DEGRADATION (多目标聚集) → MRM-03 ✓ (§11.6)
  
  极端场景 (≥ 3 假设同时违反) → MRM-02 ✓
}

MRM-04 (抛锚) 由 M1 在极端场景下评估触发（M7 不直接建议）。
```

---

**文档完成**。本详细设计共 1320 行，涵盖 M7 Safety Supervisor 的全面功能规范，包括双轨架构、假设违反检测、MRM 命令集、与 X-axis/Reflex Arc 协作、接管回切顺序化、以及 SIL 2 合规框架。所有引用均指向 v1.1.1 §x.y 或 RFC 文档，确保与架构基线的严格一致性。

