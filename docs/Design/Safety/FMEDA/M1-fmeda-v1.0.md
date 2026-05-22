# M1 ODD/Envelope Manager — FMEDA v1.0

| 属性 | 值 |
|---|---|
| 文档编号 | MASS-L3-TDL-FMEDA-M1-V10 |
| 版本 | v1.0（D2.7 完整版，从 D2.1 stub v0.1 升级；2026-06-16）|
| SIL 目标 | SIL 2（Route 1H，HFT=1 with M7，Type B 设备）|
| 分析方法 | IEC 61508-2 §7.4.3 FMEDA（软件适配）|
| 失效分类 | λSD / λSU / λDD / λDU + CCF |
| 关闭 Finding | C P1-C-8（M1 FMEDA 不完整，≥20 条）|
| 前版本 | `docs/Design/Safety/FMEDA/M1-FMEDA-v0.1.md`（D2.1 stub，11 条，git 历史保留）|

## M1 子系统分解

| 子系统代码 | 名称 | 主要职责 |
|---|---|---|
| ODD-SM | ODD 状态机 | ODD-A/B/C/D 状态管理 + 转换逻辑 + Conformance Score |
| TDL-EST | TDL 估算器 | TDL/TMR 计算 + ODD×H 权重 + 时间窗口压缩 |
| MSG-SCHED | 消息调度器 | DDS 订阅/发布 + QoS 监控 + stamp 验证 |
| CAP-MFT | Capability Manifest 读取器 | 船型参数装载 + 有效性校验 + 版本检查 |
| HLTH-MON | 模块健康监控器 | 上游模块心跳检测 + 降级决策 + 向 M7 报告 |

## FMEDA 失效模式表

| FM-ID | Subsystem | Failure Mode | Local Effect | System Effect | Detection Method | DC% | Safe Failure? | SIF Reference | Severity | HARA Reference | Notes |
| FM-M1-01 | HLTH-MON | M1 主节点线程冻结（无心跳输出）| ODD 状态停止更新；无 /sil/m1/odd_state 发布 | 全系统失去调度枢纽；M4/M5/M6 无 ODD 上下文 | M7 看门狗 100ms 超时检测 | 99% | N | SIF-01 | C3 | H-03 | D2.1 stub #1；λDD SPF |
| FM-M1-02 | ODD-SM | ODD 子域分类错误（ODD-A 误判为 ODD-C）| 港内参数（CPA=0.3nm, ≤2kn）误用于开阔海域 | 避碰决策采用错误参数集；碰撞风险 | M2 地理围栏交叉验证（SIL 1）+ M7 zone_type 不一致检测 | 90% | N | SIF-01, SIF-05 | C3 | H-13 | D2.1 stub #2；λDU SPF；[HAZID 校准] |
| FM-M1-03 | ODD-SM | Conformance Score 计算 NaN（输入越界）| 状态机拒绝输入；EMA 滤波器 NaN guard 触发 | 系统停滞于上一状态；保守降级（Fail-Safe）| 输入范围检查（not NaN, ∈[0,1]）| 99% | Y | SIF-01 | C1 | — | D2.1 stub #3；λSD；EDGE fallback |
| FM-M1-04 | CAP-MFT | Capability Manifest 缺失或损坏导致 TMR 查表返回 0 | TMR=0 → TDL > TMR 永远成立 | 系统过度自信；永不触发 ToR；ROC 接管窗口缺失 | Manifest 完整性校验 + schema_version 验证（启动时）| 95% | N | SIF-01, SIF-04 | C3 | H-03 | D2.1 stub #4；λDU SPF；缺失→启动即 DEGRADED |
| FM-M1-05 | ODD-SM | MRC 命令类型选择错误（水深不足场景选 MRM-01 锚泊）| MRC 命令类型与场景不匹配 | 深水区锚泊 → 走锚风险 | Echo Sounder + ENC 水深交叉验证 | 90% | N | SIF-01 | C3 | H-03 | D2.1 stub #5；λDD SPF；M7 监督 MRC 执行 |
| FM-M1-06 | HLTH-MON | M7 Safety_Alert DDS 消息丢失（partition/liveliness 超时）| M7 VETO 被忽略；event_score 不降级 | Doer 无 Checker 监督运行；安全网失效 | M7 心跳超时检测（500ms）| 99% | N | SIF-01 | C3 | H-04 | D2.1 stub #6；λDU SPF；自动 DEGRADED |
| FM-M1-07 | HLTH-MON | M1 + M7 共同电源失效（λDU CCF）| M1 节点掉电停止 | Doer + Checker 同时失效；安全网全失 | 独立供电轨监控 + 电池 UPS 完整性检测 | 70% | N | SIF-01 | C4 | H-03 | D2.1 stub #7；CCF；Z-bottom Hardware Override 外部缓解 |
| FM-M1-08 | ODD-SM | GPS + GLONASS 同时降质（λDD CCF）| 定位精度下降 > 10m；ODD zone 判断基础漂移 | 碰撞避险基于错误自船位置 | IMU + RAIM 交叉验证 | 90% | N | SIF-01, SIF-05 | C3 | H-19 | D2.1 stub #8；CCF；DR 60s → MRC |
| FM-M1-09 | CAP-MFT | YAML 解析错误：TMR 基线值误读（60s → 600s）| TMR 高估；过度延迟 ToR 触发 | 系统过度自信；操作员接管时窗虚长 | Schema 校验 + 范围检查 [30, 300] | 95% | N | SIF-01, SIF-04 | C3 | H-03 | D2.1 stub #9；λDU SPF |
| FM-M1-10 | ODD-SM | EMA 滤波器时间常数 τ 损坏（bit flip → τ=500s）| 降质响应极度延迟 | ODD 越界后 500s 才反应；异常运行期间安全余量不足 | 参数 CRC 校验（YAML 加载时）| 60% | Y | SIF-01 | C2 | H-13 | D2.1 stub #10；λSU；回退 τ=1s 保守值 |
| FM-M1-11 | MSG-SCHED | DDS M1↔M7 liveliness QoS 超时（lease_duration=500ms）| M7 VETO 无法送达 M1 | Doer 无监督运行 | DDS liveliness QoS 本地超时检测 | 99% | N | SIF-01 | C3 | H-04 | D2.1 stub #11；λDU SPF；超时 → DEGRADED |
| FM-M1-12 | ODD-SM | ODD 状态机卡死（无法响应新事件输入）| 状态机停止转换；ODD 状态冻结在最后值 | 系统以过时 ODD 状态调度；场景切换后参数不更新 | 状态机活跃度 watchdog（1s 无转换 → 告警）| 90% | N | SIF-01 | C3 | H-13 | D2.7 新增；ODD-SM 覆盖 |
| FM-M1-13 | ODD-SM | ODD 状态输出时间戳过时（stamp > 1s stale）| /sil/m1/odd_state stamp 超出有效期 | 下游 M4/M5/M6 使用过时 ODD 上下文决策 | 下游 stamp 时效检查（拒绝 > 1s 旧消息）| 95% | N | SIF-01, SIF-07 | C2 | H-13 | D2.7 新增；消息时效约束（架构 §15）|
| FM-M1-14 | TDL-EST | TCPA_min 输入数据过时（M2 上次更新 > 2s）| TDL 计算使用旧 TCPA 快照 | TDL 可能高估；ToR 触发延迟；ROC 接管窗口压缩 | M2 消息 stamp 时效检查（2s 阈值）| 95% | N | SIF-01, SIF-04 | C3 | H-21 | D2.7 新增；TDL-EST 覆盖 |
| FM-M1-15 | TDL-EST | TMR 计算数值溢出（TCPA 极短 → 除法边界）| TMR 计算结果 inf 或 NaN | TDL 比较失效；ToR 触发逻辑崩溃 | 除法保护（TCPA < ε 时直接触发 MRC）| 99% | Y | SIF-01 | C1 | H-03 | D2.7 新增；TDL-EST；Fail-Safe（直接 MRC）|
| FM-M1-16 | TDL-EST | TDL 系统性低估（w_H 权重系数配置错误）| TDL 输出持续偏低 | 频繁不必要 MRC 触发；OOTB 可用性下降 | DEMO 数据统计监控（误 MRC 率 KPI）| 40% | Y | SIF-01, SIF-04 | C1 | H-20 | D2.7 新增；保守方向失效；[HAZID 校准] |
| FM-M1-17 | MSG-SCHED | /sil/m1/odd_state 发布频率超出 200ms（刷新率不满足 DEMO-2 KPI）| M1 ODD 状态发布频率降级 | 下游模块 ODD 刷新不及时；场景切换响应延迟 | 发布时间戳监控 + M7 ODD 时效检查 | 95% | N | SIF-01 | C2 | H-13 | D2.7 新增；MSG-SCHED；DEMO-2 ≤200ms KPI |
| FM-M1-18 | MSG-SCHED | 上游消息队列溢出（M2/M3 数据堆积，FIFO 最旧数据被处理）| M1 处理过期 TCPA/WP 数据 | TDL 估算使用旧快照；场景感知延迟 | 队列深度监控 + 消息 stamp 时效双重验证 | 90% | N | SIF-01, SIF-05 | C2 | H-21 | D2.7 新增；MSG-SCHED |
| FM-M1-19 | ODD-SM | ODD 越界检测后未正确降级自主等级（D3/D4 继续运行）| ODD 越界标志位设置但自主等级未联动降级 | 船舶在 OUT-of-ODD 环境以高自主等级运行 | ODD 边界检测与自主等级仲裁器联动（M1 内部）| 90% | N | SIF-01 | C3 | H-03 | D2.7 新增；ADR-003 三轴 ODD 越界 |
| FM-M1-20 | MSG-SCHED | DDS schema_version 不匹配消息静默丢弃 | 接收方静默丢弃无法解析的 M1 消息 | 下游模块断链；无 ODD 更新 | schema_version 字段强制检查 + 版本不匹配显式告警 | 85% | N | SIF-01 | C2 | H-13 | D2.7 新增；架构 §15 IDL schema_version 强制字段 |

## SFF 初步估算（v1.0 专家判断）

| 失效分类 | 条目 | FM-ID 列表 |
|---|---|---|
| λSD（安全，可检测）| 2 | FM-M1-03, FM-M1-15 |
| λSU（安全，不可检测）| 2 | FM-M1-10, FM-M1-16 |
| λDD（危险，可检测）| 5 | FM-M1-01, FM-M1-05, FM-M1-08, FM-M1-17, FM-M1-07（partial）|
| λDU（危险，不可检测）| 11 | FM-M1-02, FM-M1-04, FM-M1-06, FM-M1-09, FM-M1-11, FM-M1-12, FM-M1-13, FM-M1-14, FM-M1-18, FM-M1-19, FM-M1-20 |
| CCF | 2 | FM-M1-07, FM-M1-08 |

**SFF 估算（Route 1H, HFT=1 with M7）：** λSD + λSU + λDD / λtotal ≈ (2+2+5)/20 = 45%  
→ Route 1H HFT=0 要求 ≥ 90%（Type B）：本模块单独不满足。  
→ **HFT=1 架构（M7 作为独立 Checker）已采用（ADR-001）**，HFT=1 要求 ≥ 60%：待 D3.3a 定量 FMEDA 精确计算。

> v1.0 为专家判断；定量 λ 值分配 + 精确 SFF 在 D3.3a 完整 FMEDA（HAZID RUN-001 数据 8/19 后）。
|---|---|---|---|---|---|---|---|---|---|---|---|---|
