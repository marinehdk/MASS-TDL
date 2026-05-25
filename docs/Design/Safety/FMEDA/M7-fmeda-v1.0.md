# M7 Safety Supervisor — FMEDA v1.0

| 属性 | 值 |
|---|---|
| 文档编号 | MASS-L3-TDL-FMEDA-M7-V10 |
| 版本 | v1.0（D3.3a 初版，专家判断；v1.1 = HAZID RUN-001 后定量，2026-08-19） |
| SIL 目标 | SIL 2（Route 1H，HFT=1 with M1，Type B 设备） |
| 分析方法 | IEC 61508-2 §7.4.3 FMEDA（软件适配）+ IEC 61508-3 软件 FMEA |
| 失效分类 | λSD / λSU / λDD / λDU + CCF |
| 关联 D 任务 | D3.3a（M7-core） |
| 方法论复用 | D2.7 FMEDA M1 v1.0（12 列 schema） |

## M7 子系统分解

| 子系统代码 | 名称 | 主要职责 |
|---|---|---|
| HC-MON | 硬约束监控器 | 6 类硬约束执行（HC-1~HC-6） |
| WD-MON | 看门狗监控 | M1–M6/M8 心跳 + 超时检测 |
| DIAG-SELF | 诊断自检 | DC 自检 + SIL 2 合规验证 |
| ALERT-GEN | 告警生成器 | Safety_AlertMsg 生成 + recommended_mrm 选择 |
| ASDR-LOG | ASDR 日志记录 | 决策日志 + SHA-256 防篡改签名 |
| RESUME-HDL | 回切处理器 | §11.9.2 回切时序 |

## FMEDA 失效模式表

| FM-ID | Subsystem | Failure Mode | Local Effect | System Effect | Detection Method | DC% | Safe Failure? | SIF Reference | Severity | HARA Reference | Notes |
|---|---|---|---|---|---|---|---|---|---|---|---|

| FM-M7-01 | HC-MON | HC-1 CPA 交叉验证漏检：三方 CPA 不一致但 M7 未告警 | CPA 检查器输出 consistent=true 但实际 CPA 偏差 >10% | 碰撞风险评估基于错误 CPA；SIF-01 安全网失效 | CPA 计算自检（测试向量）+ M2/M5 输出反向验证 | 90 | N | SIF-01 | C3 | H-04 | λDU；SPF |
| FM-M7-02 | HC-MON | HC-1 CPA 交叉验证误报：CPAs 实际一致但 M7 判定为不一致 | check_cpa_consistency 返回 inconsistent=true 但实际偏差 <10% | 不必要 MRM-03 紧急转向；OOTB 可用性下降 | 误报率 KPI 统计（>1% 触发自检） | 60 | Y | SIF-01 | C1 | H-15 | λSD；安全失效 |
| FM-M7-03 | HC-MON | HC-2 COLREGs 一致性判定错误：规则-几何真值表误判 | Rule 14 要求右转但 M7 未检测到 M5 左转 | M6 Rule 14 输出被 M5 反向几何动作执行；险近 | 真值表完整性检查（7 规则 × 2 方向 = 14 组合） | 85 | N | SIF-02 | C3 | H-01 | λDU；SPF |
| FM-M7-04 | HC-MON | HC-5 速度限制漏检：speed > limit 但比较器溢出未告警 | check_speed_limit 返回 compliant=true 但 speed > limit × 1.05 | 船舶超速运行；在 ODD 港口区速度过高 → 碰撞风险 | 浮点比较器边界测试（speed_max ± ε 验证） | 95 | N | SIF-03 | C3 | — | λDU；SPF |
| FM-M7-05 | HC-MON | HC-6 ROT 限制漏检：ROT 超出限制但阈值加载失败 | check_rot_limit 返回 compliant=true 但实际 ROT 超限 | 船舶以超出操纵性的转艏率运行 | Capability Manifest 加载校验（启动 CRC + 值域检查） | 90 | N | SIF-03 | C3 | — | λDU；SPF |
| FM-M7-06 | HC-MON | HC-MON 6 类约束整体静默失效：M7 进程运行但所有 check 函数被跳过 | 所有约束函数返回 consistent=true/compliant=true 但未执行实际检查 | Checker 完全失效；Doer 无监督运行 → SIF-01 全失 | 约束执行计数器（每次 main_loop 递增，独立 watchdog 线程监控） | 70 | N | SIF-01 | C4 | H-25 | λDU；Checker 静默失效 |
| FM-M7-07 | WD-MON | M2 心跳丢失但 watchdog 未检测（WatchdogConfig 加载失败） | WatchdogConfig::make_default() 返回零 timeout → 永不超时 | M2 失联后系统继续以过期 WorldState 运行 | WatchdogConfig CRC + 启动时自检 | 95 | N | SIF-01 | C3 | H-03 | λDU；SPF |
| FM-M7-08 | WD-MON | WatchdogMonitor 自身卡死（evaluate() 死循环） | 主循环停在 watchdog_->evaluate() 调用 | M7 无法检测任何模块超时；所有监控失效 | 独立线程 watchdog（与 M7 watchdog 线程分离的硬件定时器） | 90 | N | SIF-01 | C4 | H-04 | λDD；SPF |
| FM-M7-09 | WD-MON | 心跳超时阈值配置错误（timeout_ms = 0 → 永不超时） | WatchdogConfig timeout_ms[i] = 0 → 所有模块永远不超时 | 所有模块 watchdog 失效 | 配置值域检查（timeout_ms ∈ [50, 30000]） | 99 | N | SIF-01 | C3 | — | λDU；启动时检查 |
| FM-M7-10 | DIAG-SELF | M7 自身 DC 自检误报（DC 实际 ≥90% 但自检报告 <90%） | 自检流程中某检查项因瞬态错误误报 | 不必要降级至 D2 + MRM-01；OOTB 可用性下降 | DC 自检结果交叉验证（两套独立计算方法比对） | 60 | Y | SIF-01 | C1 | H-15 | λSD；安全失效 |
| FM-M7-11 | DIAG-SELF | DC 自检漏报（DC 实际 <90% 但自检报告 ≥90%） | 自检流程中某检查项漏报故障 | M7 在 DC 不足时继续运行；SIL 2 安全目标未满足 | X-axis Checker 外部心跳监控 + 多级自检冗余 | 70 | N | SIF-01 | C4 | H-18 | λDU；Checker 最危险场景 |
| FM-M7-12 | DIAG-SELF | ALU 测试向量被跳过（周期自检调度器故障） | 自检定时器不触发 ALU 测试向量执行 | ALU 计算错误无法检测；CPA 计算可能错误 | 测试向量执行计数器 + 时间戳记录 | 85 | N | SIF-01 | C3 | — | λDU；SPF |
| FM-M7-13 | ALERT-GEN | Safety_AlertMsg 发布超时（DDS 发送队列满） | pub_alert_->publish() 阻塞或丢弃告警 | M1 未收到告警；MRM 未触发 | DDS liveliness QoS + 本地发送超时检测 | 99 | N | SIF-01 | C3 | H-04 | λDD；SPF |
| FM-M7-14 | ALERT-GEN | recommended_mrm 索引错误（应发 MRM-03 但发了 MRM-01） | MRM 选择逻辑错误：场景-命令映射表索引越界 | 错误 MRM 执行：紧急转向场景下减速 → 碰撞 | MRM 选择逻辑一致性检查（场景-命令映射表自检） | 80 | N | SIF-01 | C3 | — | λDU；SPF |
| FM-M7-15 | ALERT-GEN | Alert confidence 字段 NaN（浮点异常传播） | 浮点除法 by zero 或 NaN 传播到 confidence 字段 | M1 仲裁器拒绝处理告警（confidence NaN → 丢弃消息） | Alert 生成时 confidence 合法性检查（not NaN, ∈ [0,1]） | 99 | N | SIF-01 | C3 | H-04 | λDD；Not-a-Number guard |
| FM-M7-16 | ASDR-LOG | SHA-256 签名密钥损坏（bit flip） | ASDR 记录签名计算使用损坏的密钥 → 签名无效 | CCS 审计链断裂；决策记录无法验证 | 签名发布前本地验证（HMAC 自检） | 90 | N | SIF-01 | C2 | — | λDU；审计证据失效 |
| FM-M7-17 | ASDR-LOG | ASDR 日志缓冲区溢出（高频告警场景） | 环形缓冲区写满后覆盖最旧记录 | 关键决策记录丢失；CCS 审计不完整 | 环形缓冲区水位线监控（>80% 告警） | 70 | Y | SIF-01 | C1 | — | λSD；Safe Failure（丢日志不影响安全功能） |
| FM-M7-18 | RESUME-HDL | M7_READY 信号提前发出（SOTIF 未稳定） | ResumeHandler 在 stable_cycle_count < 5 时发送 READY | M5 在 M7 安全监控恢复前开始输出轨迹 → 安全监控真空 | M7 SOTIF 稳定性条件检查（≥5 个连续周期无异常才发 READY） | 85 | N | SIF-01 | C3 | H-04 | λDU；回切安全真空 |
| FM-M7-19 | RESUME-HDL | M7_READY 信号未发出（超时） | ResumeHandler 卡在 kPreResumeCheck 状态不推进 | M1 超时保护触发 → D2 + MRM-01 | M1 超时保护（T0+100ms）+ ASDR 记录 | 99 | Y | SIF-01 | C1 | H-15 | λSD；安全失效（超时 → Safe State） |
| FM-M7-20 | RESUME-HDL | Resume 积分项未重置（状态残留） | ResumeHandler 发送 M7_READY 但未确认 M5 积分项已清零 | 回切后 M5 使用历史累积误差 → 瞬态不稳定 | M5 积分项重置验证（M7 读 M5 第一个 AvoidancePlan 的积分状态确认清零） | 70 | N | SIF-01 | C3 | — | λDU；瞬态不稳定 |

## SFF 初步估算（v1.0 专家判断）

| 失效分类 | 条目 | FM-ID 列表 |
|---|---|---|
| λSD（安全，可检测） | 3 | FM-M7-02, FM-M7-10, FM-M7-19 |
| λSU（安全，不可检测） | 1 | FM-M7-17 |
| λDD（危险，可检测） | 5 | FM-M7-07, FM-M7-08, FM-M7-09, FM-M7-13, FM-M7-15 |
| λDU（危险，不可检测） | 11 | FM-M7-01, FM-M7-03, FM-M7-04, FM-M7-05, FM-M7-06, FM-M7-11, FM-M7-12, FM-M7-14, FM-M7-16, FM-M7-18, FM-M7-20 |
| CCF | 1 | FM-M7-08/09（WatchdogMonitor 与主线程共因——同一进程内失效） |

**SFF 估算（Route 1H, HFT=1 with M1）：** λSD + λSU + λDD / λtotal ≈ (3+1+5)/20 = **45%**
→ Route 1H HFT=0 要求 ≥ 90%（Type B）：本模块单独不满足。
→ **HFT=1 架构（M7 + M1）已采用（ADR-001）**，HFT=1 要求 ≥ 60%。
→ 精确 SFF 待 HAZID RUN-001 (8/19) 后定量计算。

> v1.0 为专家判断；定量 λ 值分配 + 精确 SFF 在 D3.3a 后续（HAZID 后）。

## 修订记录

| 日期 | 版本 | 变更 |
|---|---|---|
| 2026-05-25 | v1.0 | 初版 FMEDA M7（D3.3a spec §6 直接翻译，≥20 失效模式，12 列，SFF 估算） |
