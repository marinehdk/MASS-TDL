# L3 TDL 危险分析与风险评估 v0.1

| 属性 | 值 |
|---|---|
| 文档编号 | MASS-L3-TDL-HARA-001 |
| 版本 | v0.1（初始版本，参数待 HAZID RUN-001 2026-08-19 校准）|
| 日期 | 2026-06-16 |
| 架构基线 | v1.1.3-pre-stub |
| 标准 | IEC 61508-1 §7.3 + IEC 61508-1 Annex D + IMO MASS Code |
| 系统边界 | L3 TDL M1–M8 全模块（含模块间 DDS 接口）|
| 显式排外 | L3↔L4 接口 / L3↔Fusion 接口 / M7 FMEDA / ALARP → 推 D3.3/Phase 4 |
| 关闭 Finding | C P0-C-1(b)（HARA 缺失）；C P0-C-3 部分（SIF-01 独立性约束体现于缓解列）|
| Owner | 安全工程师外包（主）+ 架构师（review）|

## SIF 参考列表（§11.4 + D2.7 新增）

| SIF-ID | 功能 | 主责模块 | SIL | 状态 |
|---|---|---|---|---|
| SIF-01 | ODD 边界检测 + MRC 触发链 | M1 + M7 | SIL 2 | §11.4 直接引用 |
| SIF-02 | COLREGs 推理完整性 | M6 | SIL 2 | §11.4 直接引用 |
| SIF-03 | 轨迹安全校验（→ ENC）| M5 | SIL 1 | §11.4 直接引用 |
| SIF-04 | D4 通信链路监控 | M1 + M8 | SIL 2 | §11.4 直接引用 |
| SIF-05 | 世界模型数据完整性 | M2 | SIL 1 | D2.7 新增 |
| SIF-06 | 任务计划安全边界 | M3 | SIL 1 | D2.7 新增 |
| SIF-07 | 行为仲裁安全性 | M4 | SIL 1 | D2.7 新增 |
| SIF-08 | HMI 透明性完整性（SAT-1/2/3）| M8 | SIL 1 | D2.7 新增 |

## 风险图（IEC 61508-1 Annex D 海事适配）

严重度：C1=轻微偏航 / C2=CPA < 0.3nm / C3=碰撞或人员伤害 / C4=沉船或多人伤亡  
暴露频率：F1=年级（极端气象+稠密交通）/ F2=月级（港口/TSS/能见度<0.5nm）/ F3=周级（正常航行操纵）

| 严重度 ╲ 暴露 | F1 | F2 | F3 |
|---|---|---|---|
| C1 | SIL 0 | SIL 0 | SIL 1 |
| C2 | SIL 1 | SIL 1 | SIL 2 |
| C3 | SIL 1 | SIL 2 | SIL 2 |
| C4 | SIL 2 | SIL 2 | SIL 3 |

## HARA 危险源表

| Hazard-ID | Category | Module | Failure Description | Consequence | Severity | Exposure | SIL | SIF Reference | ODD-Subdomain | Health-State | Mitigation (Design) | Residual Risk | Notes |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
