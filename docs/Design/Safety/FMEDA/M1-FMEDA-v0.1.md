# M1 ODD/Envelope Manager — FMEDA v0.1

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-FMEDA-M1-V01 |
| 版本 | v0.1 (D2.1 initial, 2026-07-06) |
| SIL 目标 | SIL 2 (Route 1H, HFT=1 with M7, Type B 设备) |
| 分析方法 | IEC 61508-2 §7.4.3 FMEDA |
| 子模块覆盖 | 5/5 (主节点, 包络评估器, 置信门控器, TMR计算器, MRC控制器) |

## 1. 失效模式分类框架

4 类基础分类（IEC 61508-2 §7.4.3）：
- **λSD** (Safe Detected) — 安全失效，可检测
- **λSU** (Safe Undetected) — 安全失效，不可检测
- **λDD** (Dangerous Detected) — 危险失效，可检测
- **λDU** (Dangerous Undetected) — 危险失效，不可检测

外加：**CCF** (Common Cause Failure) 独立条目 + SPF/Latent 标签

SIL 2 Route 1H 架构 (Type B, HFT=1): **SFF ≥ 90%** (IEC 61508-2 Table 3)
HFT=1 实现: M7 Safety Supervisor 作为独立 Checker 通道（异构硬件 + 独立代码库）

## 2. 失效模式清单 (11 条)

| # | 失效模式 | 子模块 | 类别 | 失效分类 | 影响 | DC | 缓解 |
|---|---|---|---|---|---|---|---|
| 1 | M1 线程冻结（无 heartbeat） | 主节点 | λDD | SPF | ODD 状态停止更新，全系统失去调度枢纽 | 看门狗 100ms | M7 触发 Safety_Alert → MRC；Z-bottom Hardware Override |
| 2 | ODD zone 误判（A→C 错分） | 包络评估器 | λDU | SPF | 港内参数（CPA=0.3nm, 速度≤2kn）误用于开阔海域 → 碰撞风险 | M2 地理围栏交叉验证 (SIL 1) | M7 Checker 对比 M2 水域类型 zone_type；不一致 → Safety_Alert |
| 3 | conformance_score 计算 NaN | 置信门控器 | λSD | SPF | 状态机拒绝输入 → 系统停滞 | 输入范围检查 (not NaN, ∈[0,1]) | NaN → EDGE fallback（保守降级）；EMA 滤波 NaN guard |
| 4 | TMR 查表返回 0（Manifest 缺失 / YAML 损坏） | TMR 计算器 | λDU | SPF | TDL > TMR 永远成立 → 系统过度自信，永不触发 ToR | Capability Manifest 完整性校验；schema_version 验证 | Manifest 缺失 → M1 不进 IN 状态（启动即 DEGRADED）；YAML 损坏 → 回退 baseline 60s |
| 5 | MRC 类型选择错误（锚泊 → 水深 > 50m） | MRC 控制器 | λDD | SPF | 深水锚泊 → 走锚风险 | 水深传感器交叉验证 (Echo Sounder + ENC) | 水深超限 → 回退 HeaveTo (MRM-03)；M7 监督 MRC 执行 |
| 6 | M7 Safety_Alert 未到达（DDS partition / liveliness 丢失） | 事件处理 | λDU | SPF | M7 VETO 被忽略 → Doer 无 Checker 监督 | M7 心跳超时检测 (500ms) | 心跳丢失 → M1 自动 DEGRADED；event_score × 0.7 penalty |
| 7 | 电源共因失效（M1 + M7 同源掉电） | 全模块 | λDU | **CCF** | Doer + Checker 同时失效 → 安全网全失 | 独立供电轨 + 电池 (IEC 61508-2 §7.4.3.3) | Z-bottom Hardware Override（零软件硬连线急停）|
| 8 | GNSS 星座级共因失效（GPS + GLONASS 同时降质） | 包络评估器 | λDD | **CCF** | 定位漂移 → zone 误判 | IMU + RAIM (Receiver Autonomous Integrity Monitoring) | DR 模式限时 60s → 超时 MRC；M2 own_ship confidence 下降 → M1 DEGRADED |
| 9 | YAML 参数解析错误（TMR 基线误读为 600s） | 参数加载器 | λDU | SPF | TMR 过高 → 系统过度自信，操作员接管时窗虚长 | Schema 校验 + 范围检查 [30, 300] | schema_version 不匹配 → 拒绝加载；参数超范围 → 拒绝并告警 |
| 10 | EMA 滤波器 τ 参数损坏（bit flip → τ=500s） | 置信门控器 | λSU | SPF | 降质响应延迟 → ODD 越界后 500s 才反应 | 参数 CRC 校验 (YAML 加载时) | 损坏 → 回退 τ=1s 保守值（快速响应降质）|
| 11 | DDS 通信链路中断（M1 ↔ M7 liveliness QoS 丢失） | 总线 | λDU | SPF | M7 VETO 无法送达 M1 → Doer 无监督运行 | DDS liveliness QoS (lease_duration=500ms) | 超时 → DEGRADED；M8 显示"安全监督失效"红色告警 |

## 3. SFF 初步估算

| 类别 | 计数 | 说明 |
|---|---|---|
| λSD | 2 (#3, #8 partial) | Safe detected — NaN guard, GNSS 共因可检测 |
| λSU | 1 (#10) | Safe undetected — EMA τ 损坏（非立即危险）|
| λDD | 3 (#1, #5, #8 partial) | Dangerous detected — 线程冻结, MRC 错选, GNSS 可检测 |
| λDU | 5 (#2, #4, #6, #9, #11) | Dangerous undetected — zone 误判, TMR=0, Alert 丢失, YAML 错读, DDS 中断 |
| CCF | 2 (#7, #8) | 独立条目 |

$$SFF = \frac{\lambda_{SD} + \lambda_{SU} + \lambda_{DD}}{\lambda_{total}} = \frac{2+1+3}{11} \approx 54.5\%$$

**初步评估**：SFF 估算值 (54.5%) 低于 Route 1H HFT=0 要求的 90%。**推荐 HFT=1**（M7 作为独立 Checker 通道），此时 SFF 要求降至 ≥ 60%（Type B, IEC 61508-2 Table 3）。HFT=1 下的 SFF ≥ 60% 目标有望达标（保守估计 ~67% 包含 M7 交叉检测）。

> **注**：上述为初步定性估算，待 D2.7 HARA v0.1 完成后进行定量 FMEDA（含 λ 值分配 + SFF 精确计算 + DC 量化）。

## 4. 假设与限制

- λ 值基于工业类比（Emerson 3051 FMEDA 先例, exida FMEDA Handbook V8），非 FCB 特定数据
- HAZID RUN-001 (8/19 完成) 后更新失效率数据
- D2.7 将扩充到 ≥ 20 条失效模式，补全 Annunciation / No Effect 扩展类别 + 每子模块 ≥ 2 条 SPF
- CCF 分析待 D2.7 正式 FMEDA 中补充 β-factor 模型
- 本 v0.1 仅覆盖 M1 单模块；M1+M7 联合 FMEDA 在 D3.3a

## 5. 与 D2.1 实现的对应

| FMEDA # | D2.1 缓解实现 |
|---|---|
| 1 | `M7_HEARTBEAT_TIMEOUT = 500ms` watchdog (E3) |
| 2 | zone/health-aware FSM with M2 cross-validation (B2) |
| 3 | EMA NaN guard in `compute_with_ema()` (B3) |
| 4 | ToR matrix fallback to `tmr_baseline_s` (C1); Manifest 缺失 → DEGRADED (D1) |
| 5 | `MrcTriggerLogic::can_anchor()` water depth check (existing) |
| 6 | `m7_input_stale` → score × 0.7 penalty (B2) |
| 7 | Z-bottom Hardware Override (external, not in M1 scope) |
| 8 | `m2_input_stale` → score × stale_degradation_factor (B2) |
| 9 | `parameter_loader` with schema validation and range checking (D1) |
| 10 | EMA τ from YAML; fallback to τ=1s if corrupt (B3) |
| 11 | DDS liveliness QoS with `lease_duration=500ms` on M7 topic (existing) |

## 6. 修订

| 日期 | 版本 | 变更 |
|---|---|---|
| 2026-05-21 | v0.1 | D2.1 initial FMEDA — 11 失效模式, SIL 2 Route 1H, HFT=1 with M7 |
