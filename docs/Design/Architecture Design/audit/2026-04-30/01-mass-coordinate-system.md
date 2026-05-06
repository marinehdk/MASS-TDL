# 阶段 0 · MASS v3.0 系统坐标系记录

> 本文件镜像项目 `CLAUDE.md` §2，作为审计基线。来源 `docs/Init From Zulip/‼️mass_adas_architecture_v3_industrial（Kongsberg Benchmark).html`。
> 用于审计追溯：所有跨层接口审计、命名一致性核查、阶段 4 三方对照（"v3.0 Kongsberg 立场"段）均以此为真值。
>
> 版本：v3.0 Kongsberg-Benchmarked Industrial Grade
> 镜像时间：2026-05-05

---

## 系统坐标系（CLAUDE.md §2 镜像）

本仓库是 MASS（Maritime Autonomous Surface Ship）完整系统中**仅 L3 战术层**的设计与开发。MASS 完整架构遵循 **v3.0 Kongsberg-Benchmarked Industrial Grade**：

```
Z-TOP    网络安全墙 + DMZ              IACS UR E26/E27（IT/OT 隔离 + Data Diode + DDS-Security）

Multimodal Fusion 多模态感知融合        独立感知子系统（不属 L1-L5 主决策栈）
                                       Sensors（GNSS/Gyro/IMU/Radar/AIS/Camera/LiDAR 冗余）
                                       → Fusion Pipeline → Nav Filter（15-state EKF，统一自船状态源）
                                       Feeds: L3、L4、Reflex Arc

主决策栈：
  L1  任务层 Mission Layer              [hrs~days]    — 不在本仓库（航次令、气象路由、ETA/油耗优化）
  L2  航路规划层 Voyage Planner         [min~hrs]     — 不在本仓库（ENC、WP 生成、速度剖面、安全门）
  L3  战术层 Tactical Layer             [sec~min]    ⬅⬅ 本仓库（D2/D3/D4 自主等级下 COLREGs 实时决策）
  L4  引导层 Guidance Layer             [100ms~1s]   — 不在本仓库（LOS 跟踪、漂移补偿、look-ahead）
  L5  控制分配层 Control & Allocation    [10ms~100ms] — 不在本仓库（PID/Backstepping、推力分配）

X-axis   Deterministic Checker         独立确定性验证器；对 L2/L3/L4/L5 决策具 VETO 权
                                       Doer 不能绕过；VETO 后回退 nearest compliant
Y-axis   Emergency Reflex Arc          Perception 极近距离 → bypass L3/L4 → 直达 L5（<500ms）
Z-BOTTOM Hardware Override + ASDR      零软件硬连线急停 + Extended VDR（IMO MASS 4-level 模式指示）

横向支持：Parameter Database（操纵系数/停船/吃水/风流/推进配置/降级回退）、Shore Link via DMZ（遥测+遥控接管+自主等级仲裁）
```

---

## 本仓库 L3 的接口边界（按 v3.0）

- **上游消费**：
  - L2（WP list + speed profile）
  - Multimodal Fusion（Track 级目标 + Nav Filter 自船状态）
  - Parameter Database

- **下游输出**：
  - → L4（Avoidance WP + speed adj；或 v1.0 设计中的 ψ_cmd/u_cmd/ROT — 见 finding F-P1-D4-032 跨层架构错位）

- **横向接受**：
  - X-axis Deterministic Checker 的 VETO
  - Y-axis Reflex Arc 在极端场景跳过 L3

- **横向输出**：
  - ASDR 决策日志（finding F-P1-D4-033：v1.0 §15 缺此接口）
  - Shore Link 透明性数据

L1/L2/L4/L5、Multimodal Fusion、Deterministic Checker、Cybersecurity、Sim 等其他层/轴的设计文档作为**接口参考**存在 `docs/Init From Zulip/` 内，可以读但**不要修改**——它们是其他团队的产物，本仓库职责仅是消费它们的输出契约。

---

## 与 v1.0 架构文档的差异（审计 patch list 覆盖）

v1.0 架构文档（`MASS_ADAS_L3_TDL_架构设计报告.md`）§1.2 / §6 / §7 中残留 "L1 感知层"、"L2 战略层" 等旧术语，是审计的待修补条目。**v1.0 → v1.1 patch list（08a-patch-list.md）包含字段级修复**。

具体差异点：

| v1.0 措辞 | v3.0 正确术语 | 出现位置 | finding |
|---|---|---|---|
| L1 感知层 | Multimodal Fusion 多模态感知融合（独立子系统）| §1.2 + §6 图6-1 + §6.4 多处 | F-P1-D4-019, F-P2-D1-030 |
| L2 战略层 | L2 航路规划层（Voyage Planner）| §1.2 + §7 图7-1 + §10.2 图10-1 | F-P1-D1-022, F-P2-D1-030 |
| L2 执行层（错写）| L4 引导层（Guidance Layer，下游） | §4 图4-1 + §15.2 矩阵 | F-P1-D5-012 |
| 第 15 章（IMO MASS Code）| Part 2 Chapter 1（运行环境）| §2.2 | F-P1-D1-007 |
| DMV-CG-0264（2018） | DNV-CG-0264（2025.01）| §2.3 + §14.2 | F-P1-D1-009 |
| Hareide et al.（[R23] 作者归属错） | Veitch & Alsos (2022) | §12.5 | F-P2-D1-025 |

---

## 审计基线锁定声明

本审计的所有"v3.0 Kongsberg 立场"参考均以本镜像为唯一真值。
若 CLAUDE.md §2 在审计完成后被修订，本镜像保留 2026-05-05 的快照状态用于 v1.0 → v1.1 修订溯源。
后续 v3.0 基线变更属于另一次审计的范畴。
