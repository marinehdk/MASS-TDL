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
|---|---|---|---|---|---|---|---|---|---|---|---|---|
