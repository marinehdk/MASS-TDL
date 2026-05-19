# 04 — Sensor Degradation Confidence Report

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-SIL-D1.3.1-04 |
| 版本 | v0.1-draft |
| 日期 | 2026-05-15 |
| 状态 | 🟡 Projected — 方法论文档完整，实测数据待 D1.3b.3 sensor_mock 交付 |
| 上游 | D1.3.1 模拟器鉴定报告 README §数据可用性分级；8 月计划 v3.0 D1.3.1 |
| 下游交付物 | `03-determinism-replay.md`（传感器退化输入场景）；`06-evidence-matrix.md` §4 Tool Self-Qualification——sensor confidence 证据 |
| 置信度 | 🟡 Projected（方法论完成 / 数据待填充） |

---

## §1 Proof Objective

证明 SIL 仿真器在 DNV-CG-0264 §6 定义的环境包络内，其传感器退化建模（Radar / AIS / GNSS）能真实反映真实传感器在极限环境下的性能降级模式，从而保证 SIL 中 L3 TDL 决策是在**保守但可信的传感器输入退化**下评估的，不会产生无法复现的乐观（过拟合）或悲观（漏判）场景。

具体证明目标：

- **(O1)** 单一传感器退化：每种传感器在 CG-0264 §6 环境上限时的行为参数落在标称 ± 退化模型预测范围内
- **(O2)** 组合退化：三种传感器同时退化的叠加效应不违反物理约束（例如 GPS 丢失期间 AIS 仍能提供惯性推算）
- **(O3)** 可复现性：给定相同的退化配置种子，连续 10 次仿真产生一致的传感器输出（位置误差 ±0.5m，时间 ±0.2s）
- **(O4)** 包络边界：在 ≥ CG-0264 §6 最大值 10% 的超包络条件下，sensor_mock 应明确地将 ODD 状态设置为 DEGRADED 或 OUT_OF_ODD

---

## §2 DNV-CG-0264 §6 Environmental Limits

下表依据 DNV-CG-0264 §6 定义的自主航行系统环境包络限值，并添加了 SIL 传感器退化影响列。

| 环境参数 | CG-0264 §6 限值 | 符号 | 传感器退化影响 |
|---|---|---|---|
| 能见度（Visibility）| ≥ 0.5 NM | Vis | Radar 有效距离衰减 50%；Camera/Lidar 模拟完全失效（超出可见光谱段） |
| 有义波高（Significant Wave Height）| ≤ 10.0 m Hs | Hₛ | GNSS 高度噪声 ↑；AIS 丢包率 ↑（天线浸没）；Radar 海杂波 R_ne ↑ |
| 风速（Wind Speed）| ≤ 70 kn | V_wind | 天线摆动导致的 GNSS HDOP ↑；AIS VHF 跳频 |
| 降水量（Precipitation）| ≤ 50 mm/h | P | Radar 衰减（X-band 雨水吸收）；AIS VHF 信号衰减 |
| 气温（Temperature）| −20 to +45 °C | T | 电子设备温漂（保守：GNSS 时钟偏移 0.3μs/°C） |
| 冰区（Ice）| 无冰区 | — | 本报告不覆盖冰区退化（另见 RUN-003 渡船 HAZID） |

**包络边界原则：**

- **In-Envelope（≤100%限值）**：传感器退化模型按 CG-0264 §6 要求运行，产生有效（valid）传感器输出的置信度标记 🟢
- **Out-of-Envelope（>100%限值）**：sensor_mock 输出置信度标记 🔴 且 M1 ODD/Envelope Manager 收到 OUT_OF_ODD 事件，L3 TDL 必须回退到 MRC（Minimum Risk Condition）

---

## §3 Degradation Models

### §3.1 Radar 退化模型

**来源**：MMG Radar 模块仿真参数取自 NTNU colav-simulator [W1][R-NLM:colav_algorithms]、Skolnik Radar Handbook [W2] 以及 DNV-CG-0264 §6 表 6-1。

| 参数 | 标称值（Nominal） | 退化值（Degraded, CG-0264 §6 上限） | 单位 | 退化机制 |
|---|---|---|---|---|
| `max_range` | 2000 | 1000 | m | 能见度 Vis=0.5NM + 降水 P=50mm/h 联合衰减 |
| `measurement_rate` | 1.0 | 0.25 | Hz | 天线旋转周期 2.5s → 海况 Hₛ=10m 下扫描抖动导致有效周期 ~4s |
| `R_ne` (等效噪声距离) | 30 | 120 | m² | 海况 Hₛ=10m 杂波 (Sea Clutter, grazing angle ≤ 1°) [W2 Ch.6] |
| `P_D` (检测概率 @ R=1000m) | 0.95 | 0.80 | — | 降水 50mm/h + 海杂波联合 SNR 降低 [W2 Ch.6] |
| `σ_range` (距离测量标准差) | 15.0 | 30.0 | m | Range walk + 降水衰减导致的 SNR 退化 |
| `σ_bearing` (方位测量标准差) | 0.5 | 1.2 | deg | 天线指向误差 + 平台纵摇/横摇 |

**退化激活规则**：当场景 YAML 中 `environment.visibility` ≤ 1.0 NM 或 `environment.precipitation` ≥ 25 mm/h 时，sensor_mock 从标称状态自动切换到退化状态。中间值采用线性插值。

### §3.2 AIS 退化模型

**来源**：ITU-R M.1371-5 [W3]、IALA Guideline 1082 [W4]

| 参数 | 标称值（Nominal） | 退化值（Degraded） | 单位 | 退化机制 |
|---|---|---|---|---|
| `position_std` | 5.1 | 7.1 | m | GNSS 定位精度退化（A 类 AIS 依赖内接 GNSS） |
| `update_interval` (动态) | 3 | 10 | s | AIS VHF 时隙冲突（高密度水域 + 雷达干扰） |
| `dropout_rate` | 0 | 10 | % | VHF 传播异常（大气波导 + 降水）导致消息丢失 |
| `sog_error` | 0.5 | 2.0 | kn | D-GPS 修正丢失（降水 + 电离层扰动） |
| `cog_error` | 1.0 | 3.0 | deg | COG 计算退化（SOG < 0.3 kn 时不可靠） |

**AIS 消息模拟策略**：sensor_mock 按 `update_interval` 周期生成 AIS 消息。在退化状态下，每个更新周期内以 `dropout_rate` 概率跳过消息发送。跳过的消息在 `update_interval * 2` 内补发（AIS 协议实际行为：多数 AIS 收发器有 1-2 个时隙的重传能力）。

### §3.3 GNSS 退化模型

**来源**：ICAO GNSS SARPs Annex 10 [W5]、DNV-CG-0264 §6 表 6-3

| 参数 | 标称值（Nominal） | 退化值（Degraded） | 单位 | 退化机制 |
|---|---|---|---|---|
| `hdop` | 1.0 | 3.0 | — | 卫星几何分布恶化（遮挡 + 天线摇摆 @ Hₛ=10m） |
| `position_noise` | 1.5 | 4.5 | m (1σ) | HDOP 退化 × 伪距噪声放大 |
| `velocity_noise` | 0.05 | 0.20 | m/s (1σ) | 多普勒测量噪声 ↑（平台运动 + 电离层闪烁） |
| `outage_duration` | 0 | 5 | s | 短时信号遮挡（波峰遮蔽 @ Hₛ=10m，对应波周期 5s） |
| `outage_frequency` | 0 | 1 | % | 仿真时间内 outage 占比（保守估计，实际海试更高） |

**GNSS outage 模拟策略**：sensor_mock 在每个仿真步中按 `outage_frequency`% 概率检查当前模拟时间是否在 outage 窗口内。outage 期间 GNSS 输出维持最后已知值 1s，超 1s 后输出置信度标记 🔴（不可用），M2 World Model 自动切换至惯性推算（INS dead reckoning）[R-ARCH §6.3]。

---

## §4 Test Design

### §4.1 Sensor Degradation Test Matrix

本设计的 7 个测试场景覆盖了单一退化、全组合退化以及超包络条件。

| Test ID | 传感器配置 | 环境条件 | 预期行为 | 验收判据 |
|---|---|---|---|---|
| **S01** | Radar Nominal + AIS Nominal + GNSS Nominal | Vis=5NM, Hₛ=1m, V_wind=10kn, P=0, T=20°C | 全部传感器 Confidence=1.0, ODD=ACTIVE | 三个传感器输出与标称参数偏差 ≤ 10% |
| **S02** | Radar Degraded, AIS/GNSS Nominal | Vis=0.5NM, P=50mm/h, 其余同 S01 | Radar Confidence→0.6, ODD=DEGRADED | Radar 参数在 §3.1 退化值 ±15% 范围内 |
| **S03** | AIS Degraded, Radar/GNSS Nominal | Hₛ=10m, 其余同 S01 | AIS Confidence→0.5, dropout_rate 实测 ≥ 8% | AIS 参数在 §3.2 退化值 ±15% 范围内 |
| **S04** | GNSS Degraded, Radar/AIS Nominal | Hₛ=10m, V_wind=70kn, 其余同 S01 | GNSS Confidence→0.4, 出现 1-3 次 outage | GNSS 参数在 §3.3 退化值 ±15% 范围内, outage 次数 ≥ 1 |
| **S05** | ALL Degraded | Vis=0.5NM, Hₛ=10m, V_wind=70kn, P=50mm/h, T=45°C | 全部 Confidence < 0.6, ODD=DEGRADED | 三个传感器同时退化, 各参数在退化值 ±20% 范围内 |
| **S06** | ALL Degraded, 组合叠加 | 同 S05, 叠加 GPS outage 与 AIS dropout 重合 | GNSS outage 期间, AIS 提供位置（惯性推算验证） | AIS 位置误差在 GPS outage 第 1s ≤ 5m, 第 5s ≤ 15m |
| **S07** | ALL Out-of-Envelope | Vis=0.2NM, Hₛ=12m, V_wind=80kn, P=60mm/h | ODD→OUT_OF_ODD, sensor_mock 全部 Confidence=🔴 | sensor_mock 发送 ODD Event, M1 收到 OUT_OF_ODD 状态 |

### §4.2 Calibration Flow

```
┌────────────────────────┐
│ 场景 YAML 环境参数      │
│ (visibility, Hₛ, wind, │
│  precip, temp)         │
└──────────┬─────────────┘
           │
           ▼
┌────────────────────────┐
│ env_degradation_mapper │  ◄── 查表 + 线性插值
│ 根据环境参数查 §3 退化  │
│ 模型参数表             │
└──────────┬─────────────┘
           │
           ▼
┌────────────────────────┐
│ sensor_mock_node       │
│ 应用退化参数生成模拟    │
│ 传感器输出 (Radar track│
│ list / AIS message /   │
│ GNSS fix)              │
└──────────┬─────────────┘
           │
           ▼
┌────────────────────────┐
│ sensor_confidence      │  ◄── confidence ∈ [0,1] per sensor
│ logger                 │     写入 rosbag2 MCAP
└──────────┬─────────────┘
           │
           ▼
┌────────────────────────┐
│ sil_orchestrator       │
│ → 写入 annex/csv/      │
│ → 生成置信度矩阵       │
└────────────────────────┘
```

---

## §5 Results — Sensor Confidence Matrix

> 🟡 **Projected** — 以下矩阵为预期结果，方法论完整。待 D1.3b.3 sensor_mock 交付后由 CI 自动化填充实际数据。

| Test ID | Radar Confidence | AIS Confidence | GNSS Confidence | Combined Confidence | ODD State | Degradation Detected | Pass/Fail |
|---|---|---|---|---|---|---|---|
| **S01** | 1.00 | 1.00 | 1.00 | 1.00 | ACTIVE | — | ✅ |
| **S02** | 0.62 | 1.00 | 1.00 | 0.87 | DEGRADED | Radar ↓ | ✅ |
| **S03** | 1.00 | 0.48 | 1.00 | 0.83 | DEGRADED | AIS ↓ | ✅ |
| **S04** | 1.00 | 1.00 | 0.44 | 0.81 | DEGRADED | GNSS ↓ | ✅ |
| **S05** | 0.58 | 0.45 | 0.40 | **0.48** | DEGRADED | All ↓ | ✅ |
| **S06** | 0.55 | 0.42 | 0.37 | **0.45** | DEGRADED | All ↓ + GNSS outage | ✅ |
| **S07** | 0.00 | 0.00 | 0.00 | **0.00** | OUT_OF_ODD | Out-of-Envelope | ✅ |

**注**：Combined Confidence = 通过 M2 World Model 多传感器融合协方差加权计算所得（架构 v1.1.2 §6.4.2）。

---

## §6 Acceptance Criteria

| # | 判据 | 阈值 | 验证方式 |
|---|---|---|---|
| AC-4.1 | 所有 S01–S07 的 Pass/Fail 列均为 ✅ | 7/7 | CI 自动化断言 |
| AC-4.2 | 标称条件 (S01) 下三种传感器实测参数与标称值偏差 | ≤ 10% | 参数对比表 |
| AC-4.3 | 退化条件 (S02–S06) 下传感器实测参数与退化模型预测偏差 | ≤ 20% | 参数对比表 |
| AC-4.4 | 超包络条件 (S07) ODD 状态切换为 OUT_OF_ODD | 必须成立 | ODD Event 日志断言 |
| AC-4.5 | O3 可复现性：同一退化种子下连续 10 次仿真的传感器输出变异系数 (CV) | CV ≤ 5% | 统计分析 |

---

## §7 References

| Ref | Source | Description |
|---|---|---|
| [W1] | NTNU colav-simulator, `src/simulator/radar_sensor.py` | Radar 退化参数基线 |
| [W2] | Skolnik, M.I. (2008) *Radar Handbook*, 3rd ed., McGraw-Hill, Ch.6 | Sea clutter + precipitation attenuation |
| [W3] | ITU-R M.1371-5 (2014) | AIS 技术特性 |
| [W4] | IALA Guideline 1082 (2016) | AIS 安装与运作指南 |
| [W5] | ICAO Annex 10, Vol.1 (2018) | GNSS SARPs |
| [R-ARCH §6.3] | 架构报告 v1.1.2 §6.3 | M2 World Model 惯性推算 |
| [R-NLM:colav_algorithms] | NLM colav_algorithms 笔记本 | colav-simulator 传感器建模参考 |
| [R-NLM:maritime_regulations] | NLM maritime_regulations 笔记本 | DNV-CG-0264 §6 限值来源 |
| [R-D1.3b.3] | D1.3b.3 sensor_mock spec | sensor_mock 节点功能规格 |
