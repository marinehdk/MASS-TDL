# SIL Coverage Metrics Methodology

| Field | Value |
|---|---|
| Document ID | MASS-L3-TDL-SIL-COV-001 |
| Version | v1.0 |
| Date | 2026-05-20 |
| Status | D1.7 deliverable — Part 1 |
| Architecture Baseline | v1.1.3-pre-stub |
| Development Plan | v3.2-master |

## §1 覆盖率元方法论（Coverage Model）

### 1.1 三轨并行覆盖策略

L3 TDL SIL 场景覆盖采用 **"MC/DC 结构覆盖 + 1100-cell 演绎立方体 + Monte Carlo 统计覆盖"** 三轨并行策略，对标 DNV-CG-0264 §3.4（Verification Coverage）和 ISO 21448 §6（SOTIF triggering condition completeness）：

| 轨 | 方法 | 覆盖目标 | 适用阶段 |
|---|---|---|---|
| **MC/DC 结构覆盖**（Modified Condition/Decision Coverage） | 对 M6 COLREGs Reasoner 中每条 Rule（5/6/7/8/9/13/14/15/16/17/19）的决策树分支条件进行独立真假值遍历，确保每个条件独立影响决策结果 | 11 Rules × 决策树平均 4–8 branch = 44–88 独立条件 | DEMO-1 Phase 1（rule-level mapping）→ DEMO-2（condition-level，D2.4 scoring dependency）|
| **1100-cell 演绎立方体**（Rule × ODD × Disturbance × Seed） | 在 4-D 参数空间格点采样，保证每个维度组合都有代表性 case | 1100 cells（11 Rule × 4 ODD × 5 Disturbance × 5 Seed） | DEMO-1 32/1100 lit → DEMO-3 1100/1100（D3.6）→ Phase 4 1100 + ≥10000 Monte Carlo |
| **Monte Carlo LHS/Sobol 统计覆盖** | 对关键连续参数（target bearing, SOG, 感知噪声 σ, 风流强度）进行 Latin Hypercube / Sobol 低差异抽样，输出 pass rate 95% CI | ≥10000 samples（D3.6） | D3.6 实跑 → Phase 4 扩展 |

**三轨协作逻辑**：MC/DC 保证代码级决策分支的完备性（白盒），1100-cell 立方体保证场景参数的格点覆盖（灰盒），LHS/Sobol Monte Carlo 保证连续参数空间的统计覆盖（黑盒）。三层递进，从确定性覆盖到统计覆盖渐次收敛。

### 1.2 与海事规范的对齐

覆盖率方法论文档直接回应以下规范条款：

| 规范 | 条款 | 覆盖映射 | 证据 |
|---|---|---|---|
| CCS《智能船舶规范 2024》§9.1.2 | 自主航行功能验证须证实系统在指定 ODD 内的正确性 | 立方体 4 ODD 维度（open_sea / coastal_traffic_separation / port_approach / offshore_wind_farm）是 ODD 规范条款的实例化 | 本节 §2 立方体定义 |
| CCS《智能船舶规范 2024》§9.1.3 | 避碰功能验证须覆盖 COLREGs 适用规则 | 11 Rule 覆盖率矩阵（§2 Rule 维）直接回应"覆盖适用规则"要求 | 本节 §2.1 Rule 维枚举 |
| ISO 21448 §6.4 | 触发条件（triggering condition）的完备性枚举 | SOTIF 触发条件覆盖表（§3）穷举三类触发条件：Known Unsafe / Perceived-Environment Uncertainty / System Capability Edge | 本节 §3 |
| DNV-CG-0264 §3.4.2 | Verification coverage shall demonstrate systematic exploration | 立方体格点（1100-cell 等间距采样）+ LHS（拉丁超立方统计采样）混合方案满足"系统性探索"要求 | 本节 §2 + §6（Part 2）|

**置信度**：🟢 High —— 四个标准条款均明确可追溯至具体的覆盖率维度定义。CCS 接受度待 D1.8 early letter 发函确认后从 🟢 升级为 ⚫ 锁定。

### 1.3 覆盖率度量核心公式

对于任意测试套件 S 在覆盖空间 C 上的覆盖率，定义：

```
覆盖率(S, C) = |{c ∈ C | ∃ s ∈ S 覆盖 cell c}| / |C|
```

其中 cell `c = (rule, odd, disturbance, seed)` 为立方体中的一个格点。对于 Monte Carlo 扩展，每个 sample 映射到最邻近的格点做"软覆盖"统计。

---

## §2 1100-Cell 覆盖立方体定义

### 2.1 四维参数空间

覆盖立方体由四个离散维度（Rule × ODD × Disturbance × Seed）张成，每个维度各有明确的枚举值和物理意义：

| 维度 | 枚举 | 计数 | 来源 / 依据 |
|---|---|---|---|
| **Rule 维** | Rule 5（Lookout）/ Rule 6（Safe Speed）/ Rule 7（Risk of Collision）/ Rule 8（Action to Avoid Collision）/ Rule 9（Narrow Channel）/ Rule 13（Overtaking）/ Rule 14（Head-on）/ Rule 15（Crossing, give-way）/ Rule 16（Action by give-way vessel）/ Rule 17（Action by stand-on vessel）/ Rule 19（Restricted Visibility）| 11 | COLREGs Part B §I–III — 避碰核心守则；Rule 5–9 为通用义务（Part B §I），Rule 13–17 为相遇行为（Part B §II），Rule 19 为能见度不良（Part B §III） |
| **ODD 维** | `open_sea` / `coastal_traffic_separation` / `port_approach` / `offshore_wind_farm` | 4 | 架构 v1.1.3-pre-stub §F.6；对应 CCS ODD 规范的四个操作子域 |
| **Disturbance 维** | `bf_0_1` / `bf_2_3` / `bf_4_5` / `bf_6_7` / `sensor_degraded` | 5 | Beaufort 风级 0–7 分档 + 感知降质档位 |
| **Seed 维** | `seed_1` / `seed_2` / `seed_3` / `seed_4` / `seed_5` | 5 | 保证可复现性的随机种子；每格点 5 seed 使得 cell 内部扰动初值有基本统计意义 |

总 cells = 11 × 4 × 5 × 5 = **1100**。

### 2.2 Disturbance Bin 详细定义

Disturbance 维的 5 个 bin 基于 Beaufort 风级与能见度组合定义，覆盖从静风良好能见度到传感器降质的最坏情况：

| Bin 标签 | Wind (kn) | 对应 Beaufort 级 | Visibility (m) | 含义 |
|---|---|---|---|---|
| `bf_0_1` | 0–3 | Bf 0–1（静风/轻风）| ≥ 5000 | 微风或静风，能见度良好；典型理想测试条件 |
| `bf_2_3` | 4–10 | Bf 2–3（轻风/微风）| ≥ 5000 | 小风，能见度良好；大多数常规航行条件 |
| `bf_4_5` | 11–21 | Bf 4–5（和风/清风）| ≥ 5000 | 中风力，甲板工作开始受影响；FCB 常规操作上限以内 |
| `bf_6_7` | 22–33 | Bf 6–7（强风/劲风）| ≥ 5000 | 大风力，甲板行走困难；FCB 设计操作上限 |
| `sensor_degraded` | 任意 | 任意 | < 5000（雾/雨/雪）或 感知噪声 σ × 3 或 感知帧率 < 50% | 感知降质（视觉受限或传感器信号劣化）；最坏情况 |

`sensor_degraded` 触发的优先规则：visibility < 5000m 直接判为 sensor_degraded 而无需参考风力；visibility ≥ 5000m 但感知噪声均值超过标称值 3 倍（如 Radar 距离噪声 σ 从 1m 升至 3m 以上）也判为此档。

### 2.3 DEMO 阶段覆盖目标

| 阶段 | 覆盖 cell 数 | 覆盖率 | 说明 |
|---|---|---|---|
| DEMO-1（Phase 1, 6/15）| 32 / 1100 | 2.9% | 22 Imazu 标准场景 + 10 自编场景；合理 baseline，展示热图工具已验证 |
| DEMO-2（Phase 2, 7/31）| 385 / 1100 | 35% | Rule 5–8 + 13–15 全部覆盖，每 Rule × 至少 1 ODD × 1 Disturbance |
| DEMO-3（Phase 3, 8/31）| 1100 / 1100 | 100% | 全立方体格点覆盖（含 Rule 9/16/17/19 + sensor_degraded 档）|
| Phase 4（D4.x, 9–12月）| 1100 + ≥10000 Monte Carlo | 100% + 扩展 | 1100 格点维持 + 额外 ≥10000 LHS/Sobol 统计补全（连续参数空间）|

DEMO-1 的 32/1100 覆盖率虽然数字小，但提供了一个可观测、可验证的增长 baseline，使后续阶段的覆盖率进展可追踪可审计。热图工具的输出（见 `scenarios_coverage.py`）以红—黄—绿渐变色直观展示每 (Rule, ODD) 格点的填充状态。

---

## §3 SOTIF Triggering Condition 覆盖

### 3.1 三类穷举（ISO 21448 §6.4）

依据 ISO 21448（SOTIF）§6.4 要求，对 L3 TDL 在操作过程中可能遇到的触发条件（triggering condition）进行三类别系统化枚举：

| 类别 | ISO 21448 定义 | 本系统实例化 | 目标数量 | 来源 |
|---|---|---|---|---|
| **Category A**: Known unsafe scenarios（已知不安全场景）| 系统功能限制已知且已在规范中记载的场景 | COLREGs 违反或碰撞几何已知的测试场景 | ≥ 15 | Imazu-22 基线 + COLREGs 专家经验 |
| **Category B**: Perceived-environment uncertainty（感知环境不确定性）| 感知系统与其环境模型之间的失配导致的触发条件 | AIS/Radar/GPS/Camera 感知降质或误导 | ≥ 20 | HAZID RUN-001 感知失效分析 + ISO 21448 Annex B 参考清单 |
| **Category C**: System capability edge cases（系统能力边界）| 算法或系统性能在 ODD 边界附件退化的触发条件 | MPC 不收敛、多目标饱和、数学优化边界 | ≥ 15 | 架构评审 124 findings + 设计极限分析 |

**总计**：≥ 50 条 SOTIF triggering condition。

### 3.2 Category A 详细清单（Known Unsafe）

| # | 触发条件 | 涉及 Rule | 来源 |
|---|---|---|---|
| A01 | Head-on 相遇，自船无行动（stare-and-pray 行为）| Rule 14 | Imazu-22 #01 |
| A02 | Crossing 相遇，give-way 船延迟行动超过 TCPA 阈值的 30% | Rule 15 | Imazu-22 #05 |
| A03 | Overtaking 在狭窄水道中给被追越船预留空间不足 | Rule 13, Rule 9 | Imazu-22 #09 |
| A04 | 多目标（3+ vessel）同域相遇超出单 Rule 处理能力 | Rule 5, Rule 7 | 架构评审 finding E-P0-E3 |
| A05 | 直航船在 in extremis 时刻之前过早采取避让动作（stand-on 违规）| Rule 17 | 专家经验 |
| A06 | 能见度不良条件下保持正常航速不减速 | Rule 19 | Imazu-22 #12 |
| A07 | 追越船从右侧追越（违反常规）导致 CPA 不足 | Rule 13 | 专家经验 |
| A08 | 交叉相遇中让路船转左（违反 COLREGs 要求转右）| Rule 15 | 架构评审 |
| A09 | 航道内迎航向迎面相遇时靠左（违反靠右习惯）| Rule 9, Rule 14 | 专家经验 |
| A10 | 避碰行动幅度 < 15°（Rule 8 要求"大幅"行动）| Rule 8 | 行业常见错误 |
| A11 | 多行动累积效应导致意外路径（蟹行或 S 形路径）| Rule 8 | 仿真发现 |
| A12 | 对 AIS 目标无 CPA/TCPA 计算仅依赖 Radar（Rule 7 违规）| Rule 7 | 专家经验 |
| A13 | 在 TSS（分道通航制）中逆行 | Rule 10（通过 Rule 9 映射）| 架构评审 |
| A14 | 避让后过早恢复原航向导致二次危险 | Rule 8 | 经验模式 |
| A15 | 对静止目标（渔网/浮标）错误应用 COLREGs 规则 | Rule 5 (Lookout) | 实务经验 |

### 3.3 Category B 详细清单（Perceived-Environment Uncertainty）

| # | 触发条件 | 涉及感知子系统 | 来源 |
|---|---|---|---|
| B01 | AIS 目标信号丢失 ≥ 5 s 后重新捕获时身位跳变 | AIS | HAZID |
| B02 | Radar 多路径效应产生假回波（ghost target）| Radar | HAZID |
| B03 | GPS spoofing 产生 100 m 级位置偏移 | GNSS | HAZID |
| B04 | Camera 在低照度下将渔船误分类为货船（CNN 误分类）| Camera | 感知团队输入 |
| B05 | 视觉目标检测框漂移导致速度向量估计错误 | Camera/ML | 感知团队输入 |
| B06 | 多 Radar 回波融合时时间戳对齐偏差导致目标分裂 | Radar Fusion | 架构评审 |
| B07 | AIS 与 Radar 目标关联失败（ID switch）| AIS+Radar | HAZID |
| B08 | GNSS 多路径效应（城市峡谷/港口桥梁）导致自船位置跳变 | GNSS | 架构评审 |
| B09 | LiDAR 点云在雨/雾中衰减 > 60% | LiDAR | 感知团队输入 |
| B10 | ENC 水深数据与实船吃水不匹配（未测水域 grounding 风险）| ENC | 架构评审 |
| B11 | AIS 静默船舶（Class B 或关闭发射）未被跟踪 | AIS | 行业经验 |
| B12 | 运动模型匹配错误（将拖船分类为货船 → 错误预测机动能力）| M2 Track Fusion | 设计评审 |
| B13 | 海杂波（Sea Clutter）下 Radar 检测概率严重下降 | Radar | HAZID |
| B14 | Camera 镜头逆光遮挡或飞溅海水导致帧丢失 | Camera | 实务经验 |
| B15 | 多运动目标遮挡（一艘船遮挡后方目标）| Radar/Fusion | 感知团队输入 |
| B16 | 风速风向计结冰导致风流补偿错误 | MetOcean | HAZID |
| B17 | 船体角度变化导致 GNSS 天线多路径不同 | GNSS | 架构评审 |
| B18 | Radar 间歇性盲区（与船体结构/吊车位置相关）| Radar | 实务经验 |
| B19 | 传感器定时失步（AIS 更新率 2–10 s vs Radar 更新率 1–3 s 不对齐）| 全感知 | 架构评审 |
| B20 | 声纳/测深仪在浅水区回波丢失 | 声纳 | 设计评审 |

### 3.4 Category C 详细清单（System Capability Edge）

| # | 触发条件 | 涉及模块 | 来源 |
|---|---|---|---|
| C01 | 4+ 目标 multi-encounter 使 M6 COLREGs Reasoner 规则应用饱和 | M6 | 架构评审 finding |
| C02 | M5 BC-MPC 优化器在非凸约束下不收敛（连续 ≥ 3 步）| M5 | 设计极限分析 |
| C03 | ODD 边界附近安全距离参数 min CPA 接近 0.27 NM 引起频繁决策切换 | M1, M4 | HAZID |
| C04 | 目标船航速极低（< 1 kn）导致 TCPA 计算超时 | M2 | 设计评审 |
| C05 | 自船和目标船都在做避碰操作（multi-agent 耦合）的场景不可预测 | M5, M6 | 架构评审 |
| C06 | 浅水约束与避碰约束冲突（转向避让 → 进入浅水区 grounding）| M5 | HAZID |
| C07 | ENC 航道边界突变（船舶转向受限）| M5 | 设计评审 |
| C08 | 目标船不符合 COLREGs 角色分类（ambiguous bearing）| M6 | 专家经验 |
| C09 | 在时间迫近（TCPA → 0）时的紧急避让触发路径振荡 | M5 | 仿真发现 |
| C10 | M2 World Model 滑动窗口时间戳超出 15 s 公差 | M2 | 架构评审 |
| C11 | 多变量约束下 M5 Mid-MPC 代价函数不凸（目标权重冲突）| M5 | 设计极限分析 |
| C12 | 感知不确定度与 V&V 假设偏差（传感器噪声模型失效）| M7 | 安全评审 |
| C13 | 通信链路口令/超时导致 Shore Link 切换控制权过程中出现决策真空 | M3, M8 | HAZID |
| C14 | 自船速度为零（系泊/停航）时的目标避碰逻辑未明确定义 | M4 | 设计评审 |
| C15 | DEMO-3 目标 1000 场景中引入的 intelligent_vessel 之间出现互锁（协同无解）| M4, M5 | 未来扩展 |

### 3.5 SOTIF 覆盖判据与 DEMO 门控

| 判据 | 条件 | 门控阶段 |
|---|---|---|
| Hard gate DEMO-3 | ≥ 50 triggering conditions 每个 × ≥ 5 seeds 独立 run，全部 PASS（或记录为 known fail 有缓解措施）| DEMO-3 |
| Soft gate Phase 4 | 在 10000 Monte Carlo samples 中，pass rate 的 95% CI lower bound ≥ 90% | Phase 4 |
| 新发现的 unknown unsafe | 进入 HAZID track → 追加到 SOTIF register → 在下一次覆盖率快照中体现 | 持续 |

**与 Imazu-22 的关系**：Imazu-22 标准场景覆盖 Category A 的核心子集（COLREGs 规则相遇基本型），但无法穷尽 Category A 的边角变体。Category B（感知不确定性）和 Category C（系统边界）需要额外的场景生成工具：D1.6 farn 参数化场景生成器用于 Category B/C 部分场景，D3.6 RL fuzzer 用于 Category C 的自动探索。HAZID RUN-001 产出的 132 项 [TBD-HAZID] 参数中，预计至少 30 项最终归类为 SOTIF triggering condition。

---

## §4 Failure Response 覆盖

### 4.1 M7 Safety Supervisor 六类硬约束故障注入

M7 Safety Supervisor 作为 Doer-Checker 架构中的独立 Checker 路径，负责检测六类硬约束故障并提供确定性响应。每类故障至少设计 10 个具体场景（方差覆盖 — 不同的 ODD、目标配置、故障时机），总场景数 6 × 10 = 60。

| # | 故障类别 | 定义 | M7 检测条件 | M7 响应动作 | 最小场景数 |
|---|---|---|---|---|---|
| F1 | M2 Track Loss | M2 World Model 对某个关键目标失去 track ≥ 1 s | M2 输出 track 的 `track_age` 字段 > 1000 ms 且 `validity_flag = FALSE` | 切换 DEGRADED 模式 → 使用线性外推 fallback（最后已知位置 + 速度）× 扩展不确定度椭圆；M7 输出 `fallback_active` 标志 | 10 |
| F2 | M5 MPC Non-Convergence | M5 BC-MPC 优化器连续 3 步返回 non-converged 状态 | M5 输出头字段 `solver_status ≠ OPTIMAL` 连续 ≥ 3 tick | 降级到 Mid-MPC 最新有效解（或进一步降级到 LOS 引导）并应用保守 CPA margin × 1.5；M7 输出 `mpc_fallback` 标志 | 10 |
| F3 | M6 Rule Contradiction | 同一 encounter 在 M6 推理中两个冲突规则同时裁定 give-way 义务（如同时 Rule 15 & Rule 17 矛盾）| M6 输出 `rule_conflict_flag = TRUE` 且 `conflicting_rules` 数组非空 | M7 进行安全优先仲裁：Rule 5（Lookout）+ Rule 8（Action to Avoid）叠加为人规则 override → 输出 `veto_override` + `override_rationale` 字段 | 10 |
| F4 | Sensor Timeout | GNSS / AIS / Radar 任一传感器数据帧超时 > 其对应的最大间隔阈值（如 AIS 20 s）| 感知总线状态头字段 `sensor_health` 中对应传感器 `status ∈ {STALE, LOST}` | 切换 DEGRADED 模式 → 使用最后已知有效值 + 扩展不确定性 → 输出 `sensor_fallback_<name>` 标志 | 10 |
| F5 | ODD Boundary Crossing | M1 ODD/Envelope Manager 两帧之间 ODD 状态出现矛盾穿越（如从 `open_sea` 直接跳到 `port_approach` 无过渡帧）| M1 输出 `odd_stable` 标志从 TRUE 变 FALSE 时触发 M7 校验相邻帧的 `envelope_id` 连续性 | 切换 CRITICAL 模式 → MRM（Minimum Risk Manuever）capture → 降速至 safe speed + 保持或退出至安全区域；M7 输出 `mrm_capture` 信号 | 10 |
| F6 | Module Heartbeat Loss | 任一模块（M1–M6, M8）在 ≥ 500 ms 内未向监控总线发布其心跳消息 | M7 内部 watcher 线程检测到模块 `last_heartbeat_ts` 与当前时间差 > 500 ms | 该模块的状态标记为 `FALLBACK` → 该模块输出被忽略并使用其最后已知有效值或默认 fallback → 若连续 ≥ 3 模块同时掉线则触发 MRM | 10 |

### 4.2 故障场景生成方法

每类故障的 10 个场景在以下维度上系统化地变化：

- **ODD 覆盖**：至少跨越 2 个 ODD 子域（如 `open_sea` + `coastal_traffic_separation`）
- **目标复杂度**：单目标 / 双目标 / 三目标
- **故障时机**：TCPA 窗口早期 / 中期 / 晚期
- **感知组合**：完全感知 / 部分降质 / 全感知但 M7 检测路径被旁路模拟

场景通过 `farn scenario generator` 的 fault injection 模块自动生成，并标注 `fault_family: F{x}` 元数据标签。

### 4.3 IEC 61508 SIL 2 诊断覆盖率

| 指标 | 目标值 | 依据 |
|---|---|---|
| SFF（Safe Failure Fraction）| ≥ 90% | IEC 61508-2 Table 3 — SIL 2 对 Type B 元件（复杂可编程器件）的架构约束 |
| 每故障类别独立 M7 检测 | 6/6 类别 | Doer-Checker 双轨不共享检测逻辑路径；M7 的检测条件、传感器输入、计算路径与 M1–M6 完全独立 |
| 诊断覆盖率（DC）计算 | FMEDA（D2.7）输出 | 每个故障类别的 DC 在 D2.7 FMEDA 中逐失效模式计算，汇总得到整体 SFF |

**FMEDA 方法论**：D2.7（5/15–7/10，安全工程师外包）对 M7 安全功能进行 Failure Modes, Effects and Diagnostic Analysis（FMEDA），逐失效模式计算诊断覆盖率。本覆盖率文档不提前预设 DC 具体数字，但在架构层面要求 M7 的检测逻辑与 M1–M6 Doer 路径的代码/数据/库三方面完全独立——这是达到 IEC 61508 SIL 2 架构约束的前提条件（见 IEC 61508-2 §7.4.3.1.3 Type B 元件 ≥ 90% SFF + 独立检测路径）。

### 4.4 DEMO 阶段 Failure Response 覆盖目标

| 阶段 | F1 | F2 | F3 | F4 | F5 | F6 | 合计 |
|---|---|---|---|---|---|---|---|
| DEMO-1（6/15）| 2 | 1 | 1 | 2 | 1 | 1 | 8 |
| DEMO-2（7/31）| 5 | 3 | 3 | 5 | 3 | 3 | 22 |
| DEMO-3（8/31）| 10 | 10 | 10 | 10 | 10 | 10 | 60 |
| Phase 4（D4.x）| 10+ | 10+ | 10+ | 10+ | 10+ | 10+ | 60+ |

DEMO-1 的 8 个场景以 F1（M2 Track Loss）和 F4（Sensor Timeout）为重点——这两类是实船最常见且 M7 实现最简单的检测路径。F3（Rule Contradiction）和 F5（ODD Boundary Crossing）依赖 M6/M1 模块交付进度，安排到 DEMO-2 中后期。

---

## §5 六维评分准则（Hagen 2022 / Woerner 2019）

### 5.1 评分架构

L3 TDL 仿真场景的评分采用 **PASS/FAIL 二元判定 + 六维连续评分双轨并存** 架构：

**PASS/FAIL 二元判定（保留现有判决）**：
- **硬通过条件（Hard PASS）**：每个 encounter 全程的最小 CPA ≥ CPA_target（0.27 NM）且无任何 full rule violation 记录
- **软通过条件（Soft PASS）**：有 1–2 次 partial rule violation 但全程最小 CPA ≥ 0.27 NM，且 weighted_total ≥ 0.70（见 HagenScorer.get_final_verdict threshold）
- **FAIL 条件**：最小 CPA < CPA_target，或出现任何 full（violated 等级）规则违反

**六维连续评分（新增 CCS 论证结构）**：

| 维度 | 符号 | 含义 | 权重 | 取值范围 | 方向 |
|---|---|---|---|---|---|
| **Safety Score** | \(s\) | 基于最小 CPA 的安全裕度连续曲线 | \(w_s = 0.30\) | [0, 1] | 越大越好 |
| **Rule Compliance Score** | \(r\) | 所有适用 COLREG 规则子准则的离散评分加权平均 | \(w_r = 0.25\) | [0, 1] | 越大越好 |
| **Delay Penalty** | \(p_{delay}\) | 决策启动相对 TCPA 参考时刻的延迟 | \(w_{p,delay} = 0.12\) | [0, 1]（clamped） | **越小越好**（penalty 项） |
| **Action Magnitude Penalty** | \(p_{mag}\) | 执行转向幅度偏离 Rule 8 "大幅"要求（30°–90°）的程度 | \(w_{p,mag} = 0.08\) | [0, 1]（clamped） | **越小越好**（penalty 项） |
| **Phase Score** | \(p\) | 让路船/直航船角色行为的阶段性合规度 | \(w_p = 0.15\) | [0, 1] | 越大越好 |
| **Trajectory Plausibility** | \(pl\) | 路径物理可行性（曲率 + 加速度限幅，防 RL "作弊"） | \(w_{plaus} = 0.10\) | [0, 1] | 越大越好 |

**总评分公式**：

\[
\text{weighted\_total} = w_s \cdot s + w_r \cdot r - \text{clamp}(0,1, p_{delay}) \cdot w_{p,delay} - \text{clamp}(0,1, p_{mag}) \cdot w_{p,mag} + w_p \cdot p + w_{plaus} \cdot pl
\]

代入默认权重：

\[
\text{weighted\_total} = 0.30 \cdot s + 0.25 \cdot r - 0.12 \cdot \text{clamp}(0,1,p_{delay}) - 0.08 \cdot \text{clamp}(0,1,p_{mag}) + 0.15 \cdot p + 0.10 \cdot pl
\]

**PASS/FAIL 独立判决逻辑**：

\[
\text{verdict} = 
\begin{cases}
\text{PASS}, & \text{if } \min(\text{CPA}) \geq 0.27 \text{ NM AND no full rule violation} \\
\text{FAIL}, & \text{if } \min(\text{CPA}) < 0.27 \text{ NM OR any full rule violation} \\
\text{SOFT\_PASS}, & \text{otherwise: } \min(\text{CPA}) \geq 0.27 \text{ NM AND weighted\_total} \geq 0.70
\end{cases}
\]

PASS/FAIL 判决与六维评分同时产出，互不依赖——判决作为合规硬门，评分作为 CCS surveyor 论证的连续论据。D2.4 扩展实现后，评分结果以 JSON 结构化日志输出到 ASDR 决策记录，与 M8 HMI 透明性接口的 SAT-1/2/3 格式对齐。

### 5.2 权重设计依据

六维权重初始值基于学术文献中的经验分布，经团队适配后按 HAZID RUN-001 和 D3.6 sensitivity analysis 分阶段校准。

| 权重 | 默认值 | 文献来源 | 备注 |
|---|---|---|---|
| \(w_s\) Safety | 0.30 | Hagen 2022 §II.C — risk-based safety weighting 建议区间 [0.25, 0.35] | 最高权重，对应 IEC 61508 SIL 2 安全优先原则 |
| \(w_r\) Rule compliance | 0.25 | Woerner 2019 Table I — COLREGs compliance 在 MIT 四属性加权中占核心 | 含 11 条规则的子准则权重内部分配（§5.3） |
| \(w_{p,delay}\) Delay penalty | 0.12 | Hagen 2022 §II.C — timeliness 因子 | 早行动（early action）在 COLREGs Rule 8 中受鼓励 |
| \(w_{p,mag}\) Action magnitude penalty | 0.08 | Woerner 2019 Table II — action magnitude 的 penalty 系数 | 权重大于 0.10 时抑制非必要大转角 → D3.6 确认 |
| \(w_p\) Phase score | 0.15 | Hagen 2022 §II.C — role-appropriate behavior 权重 | give-way vs stand-on 行为区分度来自 Imazu-22 实验数据 |
| \(w_{plaus}\) Plausibility | 0.10 | 团队扩展（Hagen/Woerner 未显式包含）；RL fuzzer 防作弊需独立维度 | 物理约束由 M5 BC-MPC 部分保证，但外部采样路径仍需过滤 |

**校准计划**：
- **HAZID RUN-001（8/19）**：对 132 [TBD-HAZID] 参数中与评分相关的 weight 敏感性参数做专家 elicitation → 输出 `[TBD-HAZID-weights]` 修订建议
- **D3.6 sensitivity analysis**：对 1100 格点全跑后的评分结果做 weight 敏感性分析（每次扰动一个 weight ±10%），观察总通过率变化幅度 → 若任何 weight 的 Δ通过率 > 5% 则启动权重重标定
- **Phase 4 Monte Carlo 扩展**：在 ≥10000 样本上验证权重稳定性

**CCS 结构覆盖信号**：
- 若 CCS《智能船舶规范 2024》§9.1.2 在 D1.8 早期沟通中要求采用规范附录中定义的固定权重结构（如 safety 必须 ≥ 0.35），则 **D2.4 实现中保留 override 机制**：`weights_override_ccs.yaml` 加载后全量替换 DEFAULT_WEIGHTS
- 覆盖判定仅针对评分方法论结构的完整性（所有 6 维必须定义），不锁定具体权重的数值

**置信度**：🟡 Medium —— 六维结构本身在 NTNU/MIT 学术圈有公开发表支撑（Hagen 2022 [R33], Woerner 2019 [R34]），但具体权重的数字依据仍以这两篇为主的局限样本，尚未在大规模 L3 TDL SIL 系统中实证校准。

### 5.3 Per-Rule 合规子准则表

每条 COLREGs 规则在评分时细化为 1–2 条可评分子准则，每个子准则独立按三档（full / partial / violated）评分后加权归入所属规则的 compliance sub-score。当一次 encounter 中同时涉及多条规则时（如 Rule 15 + Rule 8），所有适用规则的所有子准则参与评分。

| Rule | Sub-criterion | full (1.0) | partial (0.5) | violated (0.0) |
|---|---|---|---|---|
| **Rule 5** (Lookout) | 5a: 维持有效瞭望 | All-round visual + radar watch maintained continuously; all targets in M2 tracked with `confidence ≥ 0.8` | Visual/radar intermittent but no collision-critical target missed; ≤ 1 target with `confidence < 0.8` | Target undetected until CPA < 0.5 NM; watch not maintained for ≥ 10 s |
| **Rule 6** (Safe Speed) | 6a: 保持安全航速 | Own-ship SOG adjusted for visibility, traffic density, and manoeuvrability; within ODD speed envelope | SOG within 10% of safe speed threshold but no collision; slight excess corrected within 30 s | SOG consistently > ODD speed envelope for ≥ 60 s; SOG excessive for existing visibility |
| **Rule 7** (Risk of Collision) | 7a: 正确判断碰撞危险 | DCPA + TCPA computed for all tracked targets; collision risk assessed and updated every cycle; bearing change correctly used | DCPA computed but TCPA not used for ≥ 1 target; collision risk assessment delayed by > 1 cycle | Any tracked target not evaluated for collision risk; bearing-only assessment omitted |
| **Rule 8** (Action to Avoid Collision) | 8a: 大幅行动，他船可察觉 | Rudder ≥ 30° and ≤ 90° turn; executed early; speed change ≥ 20% if used | Rudder 15°–29° or > 90°; action visible but not immediately apparent; small speed change < 20% | Rudder < 15° ("wafer" turn); action undetectable by target ship; no action taken |
| **Rule 8** (Action to Avoid Collision) | 8b: 不造成二次危险 | After-action trajectory creates ≥ 0.27 NM CPA with all other vessels; no S-turn or trajectory oscillation | Post-action CPA ≥ 0.20 NM but < 0.27 NM for secondary vessel; minor trajectory overshoot corrected within 2 cycles | Post-action trajectory creates new collision risk; CPA < 0.20 NM for any other vessel |
| **Rule 9** (Narrow Channel) | 9a: 靠右航行/不阻塞 | Own-ship maintains starboard side in channel; keeps clear of restricted draft vessels; crossing ≤ 45° where possible | Temporary center-line occupation with CPA ≥ 30 m to other traffic; crossing angle > 45° but clear | Wrong-side navigation; blocking traffic; grounding risk from channel excursion |
| **Rule 13** (Overtaking) | 13a: 追越时保持足够距离 | Overtaking on give-way side (port for overtaken); stand-on vessel CPA ≥ 0.27 NM throughout; clear unambiguous passage | Overtaking on port side but CPA ≥ 0.20 NM; slight course deviation by stand-on vessel ≤ 5° | Overtaking on wrong side; CPA < 0.20 NM; stand-on vessel forced to take evasive action |
| **Rule 14** (Head-on) | 14a: 各自向右转向 | Both vessels alter to starboard ≥ 30°; consistent with anti-clockwise passing; CPA ≥ 0.27 NM | One vessel alters to starboard but second delayed > 15 s; CPA ≥ 0.20 NM | No course alteration; port-to-port passing not established; CPA < 0.20 NM; wrong-side passing |
| **Rule 15** (Crossing give-way) | 15a: 让路船义务 | Give-way vessel takes early ≥ 30° starboard turn; stand-on vessel maintains course; CPA ≥ 0.27 NM | Give-way action initiated but < 20° starboard; CPA ≥ 0.20 NM; stand-on vessel alters course unnecessarily | Give-way vessel turns port or does nothing; stand-on vessel forced into in extremis action; CPA < 0.20 NM |
| **Rule 16** (Action by give-way) | 16a: 让路船尽早大幅行动 | Give-way action taken early (TCPA remaining ≥ 60 s at action onset); ≥ 30° starboard or ≥ 20% speed reduction | Action taken when TCPA remaining 30–60 s; moderate amplitude ≥ 15° | Action taken after TCPA < 30 s or not at all; amplitude insufficient to resolve situation |
| **Rule 17** (Stand-on) | 17a: 直航船保持航向 | Stand-on vessel maintains course and speed; no course change > 5° before in extremis threshold | Course change 5°–15° before in extremis but no collision event; minor speed variation | Stand-on vessel initiates early avoidance (usurping give-way duty); significant course alteration |
| **Rule 17** (Stand-on) | 17b: 直航船 in extremis 可取行动 | If in extremis triggered, stand-on action is consistent with best COLREGs outcome; action is timely and decisive | In extremis action taken but suboptimal (delay > 5 s or amplitude < 20°) | No action taken in extremis; action taken contradicts Rule 14/15 direction; collision occurs |
| **Rule 19** (Restricted Visibility) | 19a: 能见度不良时的行动 | Speed reduced to safe for visibility; engine ready for immediate manoeuvre; action consistent with Rules 5–10 | Speed reduction < 15% in restricted visibility; all-round lookout maintained but delayed | No speed reduction; engine not ready; collision course unchanged in restricted visibility |

**评分聚合规则**：
- 每条规则内所有子准则得分的算术平均值为该规则的 compliance contribution
- 单次 encounter 中只对适用的规则集做聚合（如 overtaking 场景不评价 Rule 14/15 的子准则）
- 总 rule compliance score \(r\) = 所有适用规则的 compliance 贡献的加权平均（按规则重要性，权重暂时均等）
- 若适用规则 ≥ 2 条中有任何一条的任一个子准则为 violated（0.0），则触发 PASS/FAIL 二元判据中的 full rule violation 条件

### 5.4 Python 伪代码算法规约

以下伪代码完整定义了 `COLREGRubricScore` 类的实现规约。本伪代码与 `src/sim_workbench/sil_nodes/scoring/scoring/hagen_scorer.py` 中的 `HagenScorer` 基类框架一致——`HagenScorer` 实现了 §5.1 的基本六维线性和与累加逻辑；`COLREGRubricScore` 扩展了 per-rule 子准则评分和 YAML 可配置权重，作为 D2.4 的交付目标。

```python
"""COLREGRubricScore — D2.4 scoring engine specification pseudo-code.

Aligns with src/.../hagen_scorer.py (HagenScorer base) and extends:
  - per-rule sub-criteria scoring (§5.3)
  - YAML-configurable weights
  - PASS/FAIL/SOFT_PASS verdict
"""

import math
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple

# ── Constants ──────────────────────────────────────────────────────────────
CPA_TARGET_NM: float = 0.27          # min CPA threshold (NM)
KAPPA_MAX: float = 0.01              # max path curvature (rad/m)
ACCEL_MAX_MS2: float = 2.0           # max acceleration (m/s²)

# Default weights matching architecture doc §F.8 / §5.1
DEFAULT_WEIGHTS: Dict[str, float] = {
    "safety": 0.30,
    "rule_compliance": 0.25,
    "delay_penalty": 0.12,
    "action_magnitude_penalty": 0.08,
    "phase": 0.15,
    "plausibility": 0.10,
}

# Per-rule sub-criterion enum
SubCriterionGrade = str  # one of "full", "partial", "violated"
GRADE_MAP = {"full": 1.0, "partial": 0.5, "violated": 0.0}

# ── Data structures ────────────────────────────────────────────────────────

@dataclass
class PerRuleScore:
    rule_id: str                      # e.g. "Rule5", "Rule14"
    sub_criteria: Dict[str, SubCriterionGrade]  # {"5a": "full", "5b": "partial"}
    mean_score: float = 0.0           # arithmetic mean of sub-criteria grades

    def __post_init__(self):
        if self.sub_criteria:
            scores = [GRADE_MAP.get(v, 0.0) for v in self.sub_criteria.values()]
            self.mean_score = sum(scores) / len(scores)
        else:
            self.mean_score = 1.0      # no sub-criteria → not applicable → full

@dataclass
class ScoringResult:
    """One scored simulation run (aggregated over all timesteps)."""
    cpa_min_nm: float
    safety: float
    rule_compliance: float
    delay_penalty: float
    action_magnitude_penalty: float
    phase_score: float
    plausibility: float
    total: float
    per_rule_scores: Dict[str, PerRuleScore]
    verdict: str                      # "PASS", "SOFT_PASS", "FAIL"


# ── Main scoring class ─────────────────────────────────────────────────────

class COLREGRubricScore:
    """Six-dimension COLREGs scoring engine with per-rule sub-criteria.

    Usage:
        scorer = COLREGRubricScore()
        result = scorer.score_encounter(cpa_nm=..., rule_violations=..., ...)
    """

    def __init__(self, weights: Optional[Dict[str, float]] = None,
                 cpa_target_nm: float = CPA_TARGET_NM,
                 delay_coeff: float = 0.01,
                 kappa_max: float = KAPPA_MAX,
                 accel_max_ms2: float = ACCEL_MAX_MS2):
        self._weights = dict(DEFAULT_WEIGHTS)
        if weights is not None:
            self._weights.update(weights)         # YAML override support
        self._cpa_target = cpa_target_nm
        self._delay_coeff = delay_coeff
        self._kappa_max = kappa_max
        self._accel_max = accel_max_ms2

    # ── 6-dimension sub-scores ──────────────────────────────────────────

    def score_safety(self, cpa_nm: float) -> float:
        """§5.1 Safety: linear ramp CPA / CPA_target capped at [0,1]."""
        if self._cpa_target <= 0:
            return 1.0
        return max(0.0, min(1.0, cpa_nm / self._cpa_target))

    def score_rule_compliance(self,
                              per_rule: Dict[str, PerRuleScore]) -> float:
        """§5.3 Rule compliance: weighted average of all applicable rule scores."""
        if not per_rule:
            return 1.0
        total = sum(pr.mean_score for pr in per_rule.values())
        return total / len(per_rule)

    def score_delay_penalty(self, t_action_s: float,
                            t_target_s: float) -> float:
        """§5.1 Delay penalty: linear penalty above target threshold."""
        raw = max(0.0, t_action_s - t_target_s) * self._delay_coeff
        return max(0.0, min(1.0, raw))

    def score_action_magnitude_penalty(self, rudder_deg: float) -> float:
        """§5.1 Action magnitude penalty: quadratic deviation from 30–90° band."""
        optimal = 60.0
        half_band = 30.0
        deviation = abs(abs(rudder_deg) - optimal) - half_band
        if deviation <= 0:
            return 0.0
        return min(1.0, (deviation / half_band) ** 2)

    def score_phase(self, behavior_phase: str) -> float:
        """§5.1 Phase score: role-appropriate behavior per encounter phase."""
        phase_map = {
            "give_way":    1.0,  # give-way acting early and decisively
            "stand_on":    0.5,  # stand-on maintaining course
            "in_extremis": 0.0,  # emergency — behavior degraded
            "transit":     1.0,  # no encounter — nominal
        }
        return phase_map.get(behavior_phase, 0.5)

    def score_plausibility(self, curvature: float,
                           accel_ms2: float) -> float:
        """§5.1 Trajectory plausibility: penalty for exceeding physical limits."""
        ek = max(0.0, abs(curvature) - self._kappa_max) / self._kappa_max \
             if self._kappa_max > 0 else 0.0
        ea = max(0.0, abs(accel_ms2) - self._accel_max) / self._accel_max \
             if self._accel_max > 0 else 0.0
        return 1.0 - max(ek, ea)

    # ── Verdict ─────────────────────────────────────────────────────────

    def compute_verdict(self, cpa_min: float,
                        per_rule: Dict[str, PerRuleScore],
                        threshold: float = 0.70) -> str:
        """§5.1 PASS/FAIL/SOFT_PASS binary verdict independent of total score.

        Hard FAIL on:
          - CPA min < CPA_target (0.27 NM)
          - Any full (violated=0.0) rule violation
        """
        collision = cpa_min < self._cpa_target
        full_violation = any(
            GRADE_MAP.get(grade, 1.0) == 0.0
            for pr in per_rule.values()
            for grade in pr.sub_criteria.values()
        )
        if collision or full_violation:
            return "FAIL"

        # Compute total for SOFT_PASS consideration
        total = self.compute_total(cpa_min, per_rule, ...)
        return "SOFT_PASS" if total < threshold else "PASS"

    def compute_total(self, cpa_min: float,
                      per_rule: Dict[str, PerRuleScore],
                      t_action_s: float = 0.0,
                      t_target_s: float = 0.0,
                      rudder_deg: float = 0.0,
                      behavior_phase: str = "transit",
                      curvature: float = 0.0,
                      accel_ms2: float = 0.0) -> float:
        """§5.1 Total formula: weighted sum with penalty signs."""
        s = self.score_safety(cpa_min)
        r = self.score_rule_compliance(per_rule)
        p_delay = self.score_delay_penalty(t_action_s, t_target_s)
        p_mag = self.score_action_magnitude_penalty(rudder_deg)
        p = self.score_phase(behavior_phase)
        pl = self.score_plausibility(curvature, accel_ms2)

        return (
            self._weights["safety"] * s
            + self._weights["rule_compliance"] * r
            - self._weights["delay_penalty"] * p_delay
            - self._weights["action_magnitude_penalty"] * p_mag
            + self._weights["phase"] * p
            + self._weights["plausibility"] * pl
        )

    def score_encounter(self, cpa_nm: float,
                        rule_violations: Dict[str, Dict[str, str]],
                        t_action_s: float = 0.0,
                        t_target_s: float = 0.0,
                        rudder_deg: float = 0.0,
                        behavior_phase: str = "transit",
                        curvature: float = 0.0,
                        accel_ms2: float = 0.0) -> ScoringResult:
        """Run full scoring pipeline for one encounter.

        Args:
            cpa_nm: Minimum CPA during encounter (NM)
            rule_violations: {rule_id: {sub_criterion: grade}}
                e.g. {"Rule14": {"14a": "full"}, "Rule8": {"8a": "partial", "8b": "full"}}
            t_action_s, t_target_s: Action timing for delay penalty
            rudder_deg: Max rudder angle during action
            behavior_phase: Encounter role
            curvature, accel_ms2: Trajectory plausibility checks

        Returns:
            ScoringResult with all dimension scores and verdict.
        """
        per_rule = {
            rid: PerRuleScore(rule_id=rid, sub_criteria=crit)
            for rid, crit in rule_violations.items()
        }

        total = self.compute_total(cpa_nm, per_rule, t_action_s, t_target_s,
                                    rudder_deg, behavior_phase,
                                    curvature, accel_ms2)
        verdict = self.compute_verdict(cpa_nm, per_rule)

        return ScoringResult(
            cpa_min_nm=cpa_nm,
            safety=self.score_safety(cpa_nm),
            rule_compliance=self.score_rule_compliance(per_rule),
            delay_penalty=self.score_delay_penalty(t_action_s, t_target_s),
            action_magnitude_penalty=self.score_action_magnitude_penalty(rudder_deg),
            phase_score=self.score_phase(behavior_phase),
            plausibility=self.score_plausibility(curvature, accel_ms2),
            total=total,
            per_rule_scores=per_rule,
            verdict=verdict,
        )
```

**与 `hagen_scorer.py` 的基线关系**：
- `HagenScorer._score_safety()` / `_score_rule_compliance()` 等内部方法实现了 §5.1 的核心计算逻辑，采用 `HagenScorer.score_frame()` 逐个时间步累积分（然后取平均得最终分）
- `COLREGRubricScore` 在 D2.4 中实现以下增强：① per-rule 子准则评分（§5.3 的 13 条子准则映射），② 通过 `weights` 构造参数支持 YAML 配置文件加载（`weights_override_ccs.yaml`），③ 引入 `PerRuleScore` 数据结构以支持逐规则的审计追溯
- D2.4 完成后，`COLREGRubricScore` 将替代 `HagenScorer.score_frame()` 中的简单 `_score_rule_compliance` 字典查询，但保留 `HagenScorer` 的逐帧累加机制

---

## §6 Monte Carlo LHS/Sobol 统计覆盖方法

### 6.1 定位

Monte Carlo LHS（Latin Hypercube Sampling）和 Sobol 序列抽样是 **1100-cell 演绎立方体（§2）的统计补全层**。演绎立方体以离散格点保证组合覆盖的完备性（白盒+灰盒），Monte Carlo 方法则在连续参数空间上验证系统响应是否具有统计稳定性（黑盒）：

- **1100-cell 立方体**：每个 Cell 对应 (Rule, ODD, Disturbance, Seed) 四维离散枚举值的确定性格点，适合审计追溯和 CCS 合规论证
- **LHS/Sobol 抽样**：对 11 个连续参数（bearing, speed, range, wind, current, visibility, sensor noise 等）做分层/低差异随机抽样，产出通过率的统计置信区间

**证据链关系**：Hassani 2022 [R32] 在 AUTOSHIP 项目中同样采用"deductive cube + statistical sampling"双轨方法，其中 cube 覆盖 90% 确定性场景变体，LHS 补全其余 10% 的连续参数空间 tail。

**置信度**：🟢 High —— LHS 和 Sobol 在船海场景生成领域有多个公开发表案例支撑（[R32] Hassani 2022 §III.B, [R38] Sawada 2021 §3）；具体参数分布（§6.2）的均值/方差取值需 D3.6 执行后校准。

### 6.2 抽样参数空间

以下 11 个连续参数描述了单次 encounter 的初始条件与环境扰动。每个参数的分布类型和参数值基于 AIS 统计（Kystverket 挪威沿海数据）和 HAZID RUN-001 专家 elicitation 初步设定。

| # | 参数 | 符号 | 分布类型 | 参数 | 单位 | 物理含义 |
|---|---|---|---|---|---|---|
| 1 | Target initial bearing | \(\theta_{tgt}\) | Uniform(0, 360) | a=0, b=360 | deg | 目标船相对自船的初始方位角（真北） |
| 2 | Target SOG initial | \(v_{tgt}\) | Uniform(0, 25) | a=0, b=25 | kn | 目标船初始对地航速 |
| 3 | Target range initial | \(r_{tgt}\) | Lognormal(μ=2.0, σ=0.5) | ln-mean=2.0, ln-sigma=0.5 | NM | 目标船初始距离（对数正态右偏，3–30 NM 集中） |
| 4 | Wind speed | \(w_{spd}\) | Gumbel(μ=8, β=3) | loc=8, scale=3 | kn | 风速（极值分布，右尾含暴风事件） |
| 5 | Wind direction | \(w_{dir}\) | Uniform(0, 360) | a=0, b=360 | deg | 风向（真北；自船朝向来风方向） |
| 6 | Current speed | \(c_{spd}\) | Exponential(λ=0.5) | scale=2.0 (λ=0.5) | kn | 流速度（指数分布，以缓流为主） |
| 7 | Current direction | \(c_{dir}\) | Uniform(0, 360) | a=0, b=360 | deg | 流向（真北；流去的方向） |
| 8 | Visibility range | \(V\) | TruncatedNormal(μ=10, σ=3, lb=0.5, ub=20) | mean=10, sd=3, low=0.5, high=20 | NM | 能见度距离（截断正态，下限 0.5 NM 对应浓雾） |
| 9 | Sensor noise — range | \(\sigma_{r}\) | Uniform(0.5, 5.0) | a=0.5, b=5.0 | m | Radar/AIS 距离测量噪声标准差 |
| 10 | Sensor noise — bearing | \(\sigma_{\theta}\) | Uniform(0.1, 2.0) | a=0.1, b=2.0 | deg | Radar 方位角测量噪声标准差 |
| 11 | Own-ship SOG | \(v_{own}\) | Uniform(3, 12) | a=3, b=12 | kn | 自船对地航速（含慢速机动范围） |

**参数选择的三个原则**：
1. **COLREGs 场景敏感参数**：bearing、range、SOG 直接决定 CPA/TCPA 几何和规则适用性（Rule 13/14/15 分类依赖于 bearing 窗口）
2. **环境响应敏感参数**：wind、current、visibility 影响自船机动能力和感知系统性能，进而在扰动维度上影响通过率
3. **感知不确定度参数**：sensor noise 参数（σ_r, σ_θ）直接影响 M2 World Model 的 track 精度，是 SOTIF Category B 触发条件的量化输入

**样本量**：N = 10000 由 D3.6 执行。此样本量下，对通过率 p ≈ 0.90 的 95% 置信区间半宽约为 ±0.006（即 [0.894, 0.906]），满足统计审计需求。详见 §6.5。

### 6.3 抽样方法对比

对 11 维连续参数空间的抽样，方案对比以下三种方法（最终选择 LHS 为主、Sobol 为辅的混合方案）：

| 特性 | 简单随机抽样（SRS） | Latin Hypercube Sampling（LHS） | Sobol 低差异序列 |
|---|---|---|---|
| **分层特性** | 无分层；随机独立抽取 | 每个维度划分为 N 个等概率区间，每区间精确抽取 1 样本 | 确定性低差异序列；空间填充均匀性优于 SRS |
| **收敛速率** | \(O(1/\sqrt{N})\) | \(O(1/N)\)（一维投影均匀） | \(O((\log N)^d / N)\)（d 维时仍优于 SRS） |
| **高维效率** | 差（d ≥ 5 时出现大量空域） | 中等（d = 11 时可接受） | 良好（Sobol 专为高维设计；d ≤ 20 表现稳定） |
| **重复性** | 需固定 seed | 需固定 seed + 排列矩阵 | 确定性序列；seed 仅影响 scramble |
| **实现成本** | 极低（numpy.random 一行） | 低（scipy.stats.qmc.LatinHypercube） | 低（scipy.stats.qmc.Sobol） |
| **边缘分布适配** | 任意 | 任意（通过逆变换或 copula） | 任意（通过逆变换） |
| **适用场景** | 基线对比 | **主抽样方法** | **二级敏感性分析** |

**D3.6 选择**：
- **主抽样方法**：LHS（N = 10000）——一维投影均匀性保证了参数边缘分布的充分覆盖，对通过率 95% CI 的估计效率显著优于 SRS
- **二级抽样方法**：Sobol（N = 1000）——专用于 Sobol sensitivity indices（S1, ST）计算（见 §6.5），因为 Sobol 序列的 low-discrepancy 特性可以更高效地估计总效应指数（Total-effect index, ST）和一阶指数（First-order index, S1）
- **SRS（N = 1000）**：仅作方法对比基线保留，不用于正式覆盖报告

**工具链**：两组抽样均在 `tools/sil/lhs_sobol_sampler.py` 中实现，调用 scipy.stats.qmc 库。配置参数与分布定义在 `tools/sil/lhs_sobol_config.yaml` 中统一管理（D1.6 产出）。

### 6.4 COLREG 几何过滤器

并非所有生成的随机 encounter 都具有 COLREGs 意义。Hassani 2022 [R32] §III.B 方法对原始样本应用几何过滤器，仅保留符合以下条件的 encounters：

```python
# COLREG Geometric Filter (Hassani 2022 §III.B)
def geometric_filter(own: ShipState, target: ShipState,
                     max_dcpa_nm: float = 2.0,
                     max_tcpa_min: float = 30.0) -> bool:
    """Return True if this encounter is COLREGs-relevant."""
    dcpa, tcpa = compute_cpa(own, target)  # NM, seconds
    if dcpa > max_dcpa_nm:
        return False                          # too far — no collision risk
    if tcpa > max_tcpa_min * 60:
        return False                          # too far in time — irrelevant
    return True
```

**过滤参数取值的工程依据**：
- **DCPA ≤ 2.0 NM**：对应 CCS《智能船舶规范 2024》§7.1 碰撞危险判据中"距目标船 4 链（0.4 NM）至 2 NM"的开放水域碰撞风险评估阈值上限
- **TCPA ≤ 30 min**：对应 L3 TDL 决策时窗上限。战术层决策时间尺度为 sec–min；TCPA > 30 min 的场景应交由 L2 航路规划层处理，绕过了 L3 的战术避碰决策

**过滤后样本量估算**：
- 原始 LHS 样本量：N = 10000
- 预计通过几何过滤器的比例：20%–40%（基于 Kystverket AIS 数据统计：锚地/港口附近约 35% 的船-船对在此 DCPA+TCPA 窗口内）
- 预期有效 encounter 样本量：N_valid ≈ 2000–4000
- 这 2000–4000 个有效样本再按 COLREGs 规则分类（§6.4 分类过程），分配到 Rule 13/14/15 三类主要相遇规则的覆盖桶中

**分类逻辑**（继过滤之后）：
```python
def classify_encounter(bearing_deg: float, overtaking: bool) -> str:
    """Classify encounter by COLREGs rule (bearing convention: 0° = head-on)."""
    b = bearing_deg % 360
    if overtaking:
        return "Rule13"                # overtaking (any bearing)
    if 355 <= b or b < 5:
        return "Rule14"                # head-on
    if 5 <= b < 112.5:
        return "Rule15_giveway"        # crossing, give-way (starboard)
    if 112.5 <= b < 247.5:
        return "Rule15_stand_on"       # crossing, stand-on (port)
    return "Rule14"                    # default to head-on for ambiguity
```

### 6.5 D3.6 产出目标

D3.6（Phase 3, 7/13–8/31）执行 N=10000 LHS + N=1000 Sobol 抽样，产出以下四类统计结果：

| 产出项 | 格式 | 目标值 | 统计方法 |
|---|---|---|---|
| **Pass rate 95% CI** | `[lower, upper]` around mean | Lower bound ≥ 90%（against CPA ≥ 0.27 NM + PASS verdict）| Bootstrap percentile CI (N_resample = 10000) over 10000 LHS samples |
| **CPA_min 分布直方图** | Histogram (bin=0.05 NM, range 0–2.0 NM) + ECDF | 展示分布形状；关注左尾（CPA < 0.27 NM）占比 | Empirical distribution from 10000 samples |
| **Per-Rule 违反频率** | 表格（11 行 × 3 列：Rule / violation count / frequency） | 各 Rule 违反率 ≤ 5% | Count occurrences where any sub-criterion = violated across all encounters per Rule |
| **Sobol 敏感性指数（S1, ST）Top 5** | 排序表（5 参数 × 2 列：S1 / ST）| Top 5 敏感参数集中度 ≥ 70% | Saltelli 抽样法 over Sobol N=1000; S1=一阶效应, ST=总效应 |

**Pass rate 95% CI 的计算方法**：
```python
import numpy as np

pass_flags = np.array([1 if result.verdict == "PASS" else 0
                       for result in all_results])
n_pass = pass_flags.sum()
n_total = len(pass_flags)
p_hat = n_pass / n_total

# Bootstrap CI
rng = np.random.default_rng(42)
bootstrap_means = np.array([
    pass_flags[rng.integers(0, n_total, n_total)].mean()
    for _ in range(10000)
])
ci_lower, ci_upper = np.percentile(bootstrap_means, [2.5, 97.5])
```

**阈值合理性论证**：要求通过率 95% CI 下限 ≥ 90% 意味着在最坏统计估计下系统仍有 90% 的 encounters 通过硬 PASS 条件。剩余 ≤ 10% 的 FAIL encounters 构成 SOTIF 剩余风险，需逐一分析、记录缓解措施并追加入 HAZID 注册表。

### 6.6 DEMO-1 范围

在 DEMO-1（Phase 1, 6/15）阶段，Monte Carlo LHS/Sobol 方法学的实施仅限于以下范围：

| 交付物 | 内容 | 位置 |
|---|---|---|
| **方法论文档** | 本节 §6 全文 — 覆盖参数空间定义、抽样方法对比、过滤逻辑、产出目标 | 本文档 |
| **配置 YAML** | `lhs_sobol_config.yaml` — 11 参数分布定义 + LHS/Sobol 参数设置 | `tools/sil/lhs_sobol_config.yaml`（D1.6 同步产出）|
| **抽样脚本 stub** | 空函数签名 + 参数加载逻辑（占位） | `tools/sil/lhs_sobol_sampler.py` |
| **Dry-run 演示** | 对 2 个选定场景（Head-on + Crossing）做 N=100 dry-run LHS 抽样，验证工具链跑通 | CI 可复现；不在覆盖率正式报告中引用 |

**不在 DEMO-1 范围内**：
- ❌ N=10000 全量执行（→ D3.6）
- ❌ Sobol sensitivity indices 计算（→ D3.6）
- ❌ Pass rate 95% CI 正式报告（→ D3.6）
- ❌ 几何过滤器与立方体格点的交叉验证（→ D3.6）

**DEMO-1 评审门控**：dry-run 演示成功（工具链安装、参数加载、N=100 抽样、结果输出 CSV）即为完成。这一范围控制确保 DEMO-1 聚焦于 32/1100 立方体格点的实际覆盖率，而不是过早投入可能变更的统计框架。

---

## §7 Adversarial : Nominal : Boundary = 60:25:15 比例说明

### 7.1 比例定义与生成方法

| 类别 | 占比 | 定义 | 生成方法 |
|---|---|---|---|
| **Adversarial**（对抗性）| 60% | 刻意构造或自动化搜索的系统弱点触发场景，旨在暴露 unknown unsafe 行为 | RL fuzzer（FREA/AuthSim style 强化学习场景生成器）+ 人工长尾知识注入（5 年以上船长经验转化的 edge case）|
| **Nominal**（标称）| 25% | 落在真实 AIS 统计分布内的典型相遇场景，代表系统在"正常"条件下的运行表现 | Kystverket（挪威）和 NOAA MarineCadastre（美国）AIS 开放数据集提取 + 22 Imazu 标准场景基线 |
| **Boundary**（边界）| 15% | 系统参数部署在 ODD 边界切面上的场景，测试性能在阈值附近的退化行为 | Parameter sweep（CPA → 0.27 NM 边界, TMR → 60 s 边缘）+ Sensitivity analysis（关键参数 ± 10% 扰动）|

### 7.2 自我辩护声明

**本比例是内部工程启发式（internal heuristics），非引用外部标准。**

CCS i-Ship AIP 提交时，**不**将此 60:25:15 比例作为"规范性覆盖率要求"引用为遵循的外部规范。CCS 覆盖率合规证据以立方体格点覆盖（§2）和 SOTIF triggering condition 穷举（§3）为准。

**该比例的三点来源依据**：

1. **AV 工业文献实践**：自动驾驶领域覆盖率研究的通用做法是采用 **deductive（需求驱动）∪ inductive（数据驱动）** 混合模式，其中 adversarial 类场景通常占多数以暴露 unknown unsafe（[R32] Hassani 2022 AUTOSHIP §II 方法论论述 "adversarial sampling is more efficient than uniform for safety-critical systems"）。
2. **内部启发式经验**：项目团队在 D0 Sprint 和 HAZID 过程中发现，前 60% 的发现来自 adversarial 构造场景，25% 来自标称场景的异常统计分析，15% 来自参数边界的系统扫描。该比例反映了 L3 TDL 特定系统架构下最有效的测试资源分配方式。
3. **非认证合规论据**：该比例作为**内部场景库组合管理工具**使用（决定 RL fuzzer 的资源分配和手动场景编写的优先级），不作为 CCS 船级社认证的规范性要求。CCS 合规证据链基于 §2 的 1100-cell 演绎立方体覆盖率和 §3 的 SOTIF triggering condition 穷举。

**置信度**：🟡 Low–Medium —— 本比例基于内部工程判断（internal engineering judgment），在公开发表的外部标准（CCS / DNV / IMO / IACS）中未找到直接对应的覆盖比例要求。引用 [R32] Hassani 2022 仅作为相似方法论的间接支持，不是规范性标准。

**推翻信号**：

- 若 D3.6 覆盖扩展阶段（Phase 3, 1000 场景）实测后，Adversarial 类场景的"defect found per scenario"效率低于 Nominal 类场景，则重新调整比例为 Adversarial 40% / Nominal 45% / Boundary 15%（更多资源投入标称场景的统计覆盖）。
- 若 CCS 在 D1.8 早期沟通中提出明确的覆盖比例要求，以 CCS 要求为准。

---

## References

| 编号 | 文献 |
|---|---|
| [R32] | Hassani, V. et al. (2022). *Automatic traffic scenarios generation for autonomous ships collision-avoidance system testing*. Ocean Engineering. DOI: 10.1016/j.oceaneng.2022.111864 |
| [R33] | Hagen, T. (2022). *Risk-based Traffic Rules Compliant Collision Avoidance for Autonomous Ships*. NTNU MS thesis |
| [R34] | Woerner, K. (2019). *COLREGS-Compliant Autonomous Surface Vessel Navigation*. MIT PhD thesis |
| [R38] | Sawada, R. et al. (2021). *Automatic collision avoidance using Imazu problem*. Journal of Marine Science and Technology |
| [R11-x] | ISO 21448:2022 *Road vehicles — Safety of the intended functionality* |
| [R11-y] | IEC 61508-2:2010 *Functional safety of electrical/electronic/programmable electronic safety-related systems — Part 2* |

**Architecture Baseline**:
- v1.1.3-pre-stub §F.6（Coverage cube methodology）
- v1.1.3-pre-stub §F.8（6-dimension structured COLREGs scoring）
- v3.2-master D1.7 DoD（Coverage metrics deliverable definition）

**Related Documents**:
- V&V Plan: `docs/Design/V&V_Plan/00-vv-strategy-v0.1.md` §5（Coverage dimensions）
- Scenario Schema: `docs/Design/SIL/02-scenario-schema.md`
- HAZID RUN-001: `docs/Design/HAZID/RUN-001-kickoff.md`
- FMEDA: `docs/Design/Safety/FMEDA/`（D2.7 deliverable）
