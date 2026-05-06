# 战术决策层避碰架构调研备忘（2026-04-27）

| 属性 | 值 |
|------|-----|
| 文档编号 | RESEARCH-TACTICAL-COLAV-001 |
| 版本 | **v1.2**（v1.1 冻结后的合并修订；含 §12 船长心智模块 + §13 L3 模块结构调研 + §6.12 决策透明化 + 4 篇核心学术文献新增）|
| 日期 | 2026-04-28 |
| 作者 | 用户 + Claude（marine-research-planner 协作）|
| 状态 | **冻结**（v1.2 是后续 Spec 的唯一真源）|
| 前序版本 | v0.1 (初稿) → v0.2 (13 项 TBD 闭环) → v1.0 (用户冻结) → v1.1 (合并 Patch-A) → v1.2 (合并 Patch-B) |
| 触发 Spec | `2026-04-27-tactical-colav-design-part1-bluf-adr.md`（已产出 Spec Part 1，将依据 v1.2 升级到 Spec v1.2）|
| 影响范围 | 自主航行系统战术决策层；上接战略层（Voyage Planner），下接控制层（Trajectory/Heading Controller）；横向接 ROC 与感知融合 |
| **目标船级社** | **CCS（中国船级社）单一入级**（用户 2026-04-27 确认）|
| **首发船型** | 45m FCB（Fast Crew Boat），架构须扩展到所有船型 |
| **自主等级覆盖** | D2-D4 全谱（含降级）|

---

## 1. 问题陈述

设计自主航行系统的**战术决策层**（Tactical Decision Layer），承担船舶在 D2-D4 自主等级谱系下的避碰决策、局部重规划、模式管理与上下游接口契约。系统首发对象是 45m 级 FCB（Fast Crew Boat），但架构必须能扩展到任意船型。决策必须满足 **CCS《智能船舶规范》(2025)** 的入级要求，并对接 **IMO MASS Code（MSC 111 2026-05 通过的非强制版本）** 的目标基础与 **IACS UR E26/E27 Rev.1（2024-07-01 强制）** 的网络韧性要求。

**现状问题**：

- 业界尚无统一的"战术决策层"接口契约定义，工业界（Kongsberg / Wärtsilä / Avikus / Sea Machines）的实现各自划界、各自命名、各自对接控制层
- COLREGs 在 IMO MSC 110 / 111 阶段未做修订，自主船仍受现行 COLREGs 约束，规则解释边界（特别是 Rule 2 "良好船艺" 和 Rule 17 "保向船的最后避让"）需在算法中显式形式化
- CCS《智能船舶规范》采用 **GBS 开放式框架**（不规定具体实现，要求验证证据）+ **N / Nx / R1 / R2 / A1 / A2 / A3 标志组合**，需在架构上同时支持多组合与无缝降级
- 单船型设计往往与水动力模型耦合过紧，多船型扩展时需重写避碰核心，违反开闭原则
- D2-D4 自主等级降级路径（特别是 ROC 失联、传感器降质、关键子系统故障）未在主流学术架构中显式建模

**变更目标**：

- 输出一个**模块化、平台化、CCS 可入级**的战术决策层架构候选方案（含证据链）
- 架构必须显式支持 D2-D4 谱系切换 + Fallback 状态机
- 架构必须显式支持多船型参数化（船舶动力学插件化）
- 与战略层 / 控制层 / ROC / 感知层的接口边界明确可冻结
- 调研结论冻结后作为后续 Spec 阶段的唯一真源

**不在本调研范围**：

- 感知融合内部算法（多传感器融合、目标识别等属于感知层范畴）
- 控制层内部算法（PID / NMPC track control 等）
- 战略层航次规划具体算法（A* 全局路径、能效优化等）
- 感知/控制/通信子系统的硬件选型
- ROC 物理基础设施（HMI 设计、岸基服务器架构等）
- 网络安全实现细节（IACS UR E26/E27 + CCS《船舶网络安全指南》(2024) 合规独立调研）

---

## 2. 上游权威清单

### 2.1 项目内权威

用户确认**目前没有项目级文件**。本调研在"无项目专有约束"假设下进行；未来用户提供项目文档后需重新评估冲突项。

| 项目文档 / 章节 | 关键内容 | 是否已读 |
|----------------|---------|---------|
| 项目 CLAUDE.md | — | ❌ 用户确认无 |
| 项目 SDD | — | ❌ 用户确认无 |
| 项目 ICD（接口控制文档）| — | ❌ 用户确认无 |

**项目侧已知约束（用户口头确认）**：

- 首发船型：45m FCB，高速半滑行船型
- 系统须扩展到所有船型
- 自主等级覆盖 D2-D4 全谱（带降级）
- 战术层范围：避碰 + 局部重规划 + 模式管理 + 战略/控制接口契约 + 完整独立子系统 + 公共服务调用
- 目标船级社：**CCS 单一**

### 2.2 外部规则 / 标准（CCS 主导，DNV / ABS / BV / IACS / IMO 作为对照与补充）

| 规范 | 版本 | 相关条款 | 访问时间 |
|------|------|---------|---------|
| **CCS《智能船舶规范》(2025)** | **2025-04-01 生效** | 第 8 章（远程控制船舶）+ 第 9 章（自主操作船舶）+ 智能系统认可原则 + 远程控制站 HMI | 2026-04-27 |
| CCS《船舶网络安全指南》(2024) | 2024 版 | III 类计算机系统（含远程控制 / 自主控制）网络安全等级 | 2026-04-27 |
| CCS《船用软件安全及可靠性评估指南》| CCS 基础性指南 | 智能船舶软件安全、可靠性评估通用要求 | 2026-04-27 |
| CCS《自主货物运输船舶指南》(2018) | 2018 | 场景感知、航行控制、远程控制中心、网络安全 | 2026-04-27（参考性，FCB 非货船）|
| CCS《无人水面艇检验指南》(2018) | 2018 | USV 自主航行 / 远程遥控分级标志 | 2026-04-27（参考性，FCB 有人不适用）|
| IACS UR E26 Rev.1 | 2024-07-01 强制（500 GT 以上新建船）| 整个 UR | 2026-04-27 |
| IACS UR E27 Rev.1 | 2024-07-01 强制 | 整个 UR | 2026-04-27 |
| IACS UR E22 Rev.3 | 2023 | 计算机系统 Cat. I/II/III 分类 | 2026-04-27 |
| DNV-CG-0264 "Autonomous and remotely operated ships" | 2024 ed. | Sec.2 主原则、Sec.3 评估流程、Sec.4 NAV 功能 | 2026-04-27（参照性）|
| DNV AROS 类标志 | 2025-01-01 生效 | NAV/ENG/OPS/SAF × RC/DS/SA/FA × OB/OS/HC | 2026-04-27（参照性）|
| ABS Requirements (2024-10, Pub. 323)| 2024-10 | CONOPS goal-based 框架；AUTONOMOUS / REMOTE-CON 标志 | 2026-04-27（参照性）|
| BV NI 641 / NR 681 USV | 2017/2019 / 2022 | 4 度自动化；NR 681 USV < 500 GRT + **无人** | 2026-04-27（参照性，FCB 不属 NR 681 范畴）|
| IMO MSC.1/Circular.1455 | 通用 | 自主船审批通用框架 | 2026-04-27 |
| IMO MASS Code | 非强制版 MSC 111（2026-05）通过预期；强制版 2032 | 全部 | 2026-04-27 |
| COLREGs 1972（含修订）| 当前版本（IMO 表态 MASS Code 通过前不修订）| Rule 2 / 5 / 6 / 7 / 8 / 13-19 | 2026-04-27 |
| IEC 61508 / IEEE 1362-1998 ConOps | 通用 | SIL 分配、CONOPS 文档结构 | 2026-04-27 |

### 2.3 已知开放项

用户确认无项目级开放项编号体系。本调研产出的 TBD 由用户后续统一编号纳入项目级跟踪。

---

## 3. 工业参考实现调研

### 3.1 候选方案 A · Kongsberg K-MATE + K-Bridge / K-Pos / K-Chief 栈

**引用来源**：

- [K-MATE Kongsberg Maritime Autonomy Engine 文档（GEBCO 2017）](https://www.gebco.net/sites/default/files/documents/lee_04_2_2017.pdf) · 访问时间 2026-04-27
- [Autonomous obstacle avoidance · Kongsberg](https://www.kongsberg.com/newsroom/stories/2017/9/autonomous-obstacle-avoidance/) · 访问时间 2026-04-27
- [AUTOSEA project · Kongsberg + NTNU + DNV + Maritime Robotics](https://www.kongsberg.com/maritime/news-and-events/news-archive/2015/new-research-project-to-investigate-sensor-fusion-and-collision-avoidance-for/) · 访问时间 2026-04-27
- [Yara Birkeland 项目栈](https://www.kongsberg.com/maritime/news-and-events/news-archive/2016/automated-ships-ltd-and-kongsberg-to-build-first-unmanned-and-fully-autonomous/) · 访问时间 2026-04-27
- [Kongsberg AiP 转移关键角色到岸基（DNV，2024-06）](https://www.dnv.com/maritime/autonomous-remotely-operated-ships/regulatory/) · 访问时间 2026-04-27

**技术特征**：

- K-MATE 是统一的自主决策引擎，**与 HUGIN/MUNIN 水下平台共用架构**（共享代码库降低多船型移植成本）
- 4 模块组：Control & Guidance、Navigation & Positioning、Scene Analysis、Payload Control
- 3 操作模式：Autonomous / Supervised / Direct control
- 能力：Waypoint Following、Radar+Lidar 避碰、Virtual anchor、AUV following
- Backseat Driver 模式：K-MATE → autopilot/DP（可挂接已有控制层）
- AUTOSEA 项目（NTNU 主导）专门验证传感器融合 + COLREGs 合规导航控制

**工业落地案例**：Yara Birkeland、Hrönn、2024-06 DNV AiP（关键角色转岸基化）

**与本项目场景的适配性**：

| 维度 | 适配度 | 说明 |
|------|--------|------|
| 船型（45m FCB + 多船型扩展）| H | "Adaptable autonomy kit for almost any hull" |
| 认证目标（**CCS**）| L-M | 主战场 DNV，无明确 CCS 认证；范式可借鉴 |
| 计算资源 | M | 工业级嵌入式 |
| 与现有架构集成 | H | Backseat Driver 范式天然解耦 |
| **心智白盒映射度**（v1.2 新增）| **L-M** | 4 模块组按算法功能切分（Control & Guidance / Navigation & Positioning / Scene Analysis / Payload Control），非心智阶段；与 §12 CMM 4 阶段闭环无显式映射 |

**已知问题**：Yara Birkeland 完全无人航行延迟多年；K-MATE 闭源，自研无法直接复用代码。

### 3.2 候选方案 B · Wärtsilä NACOS Platinum + SmartMove Suite + AIM

**引用来源**：

- [Wärtsilä Voyage 概览](https://www.wartsila.com/voyage?id=492) · 访问时间 2026-04-27
- [NACOS Platinum Navigation 文档](https://www.wartsila.com/voyage/integrated-vessel-control-systems/navigation/smartquay) · 访问时间 2026-04-27
- [SmartMove + American Courage 案例](https://www.wartsila.com/media/news/13-01-2021-ship-control-redefined-wartsila-smartmove-suite-sets-sail-with-the-american-steamship-company-3253273) · 访问时间 2026-04-27
- [AIM + Lighthouse COLREGS / Chalmers 验证](https://www.wartsila.com/insights/article/the-evolving-intelligence-behind-smart-navigation) · 访问时间 2026-04-27
- [IntelliTug autonomous level 4 + ClassNK COLREGs 认证](https://www.wartsila.com/insights/article/simulating-the-future-of-maritime) · 访问时间 2026-04-27

**技术特征**：

- NACOS Platinum：集成 Navigation / Automation / Dynamic Positioning（含 BNWAS、TCS）
- SmartMove Suite 5 应用：SmartDock / SmartTransit / SmartEntry / SmartPredict / SmartDrive
- AIM：他船操纵 **15-30 分钟前瞻** + COLREGs 合规候选机动
- ClassNK 已认证 SmartMove COLREGs 功能

**工业落地案例**：MV American Courage（194m）首装；IntelliTug + PSA Polaris 拖船 autonomous level 4。

**与本项目场景的适配性**：

| 维度 | 适配度 | 说明 |
|------|--------|------|
| 船型 | M | 案例从大型船到拖船；FCB 高速段无公开案例 |
| 认证目标（**CCS**）| L | 主合作 LR / ClassNK |
| 与现有架构集成 | M | 强绑定 NACOS 平台 |
| **心智白盒映射度**（v1.2 新增）| **M** | AIM 提供 15-30 min 前瞻 + COLREGs 候选机动，接近 §12 CMM Stage 2「判」的显式建模；但 Stage 1/3/4 未独立暴露 |

**已知问题**：AIM 当前定位决策辅助（advisor），与 D4 全自主有差距。

### 3.3 候选方案 C · Avikus HiNAS Control（HD Hyundai 子公司）

**引用来源**：

- [Avikus HiNAS Control 获 DNV Type Approval（2026-04-07）](https://www.koreaherald.com/article/10711851) · 访问时间 2026-04-27
- [Avikus HiNAS 产品手册 PDF](https://avikus.ai/hubfs/45548415/HiNAS_Brochure_0823.pdf?hsLang=en) · 访问时间 2026-04-27
- [H-Line Shipping 30 船商业合同（2024-12）](https://avikus.ai/en-us/press/hd-hyundais-avikus-supplies-autonomous-navigation-solutions-to-a-large-fleet-of-30-vessels) · 访问时间 2026-04-27

**技术特征**：

- **首个跨船型量产自主航行系统获国际船级社 Type Approval**（DNV，2026-04-07）
- 三段架构（与本调研推荐架构高度对齐）：**Perception → Decision-Making → Control**
- COLREGs 形式化决策 + 视觉 + 多传感器融合（含夜间和恶劣天气）
- 定位 IMO MASS Level 2（D2，类 ADAS Level 2）
- HD Hyundai 自 2023 起标配，累计 500+ 订单
- H-Line Shipping 2024-12 首批 5 船商业合同（计划扩展 30 船）
- 9334 km 试航 15% 节油

**与本项目场景的适配性**：

| 维度 | 适配度 | 说明 |
|------|--------|------|
| 船型 | H | 跨船型量产是核心卖点 |
| 认证目标（**CCS**）| L | DNV TA 已获，CCS 未公开；架构启发价值 H |
| 计算资源 | H | 量产型 |
| **心智白盒映射度**（v1.2 新增）| **M-H** | "Perception → Decision-Making → Control" 三段流水线已有部分心智映射（Perception ≈ Stage 1 看 / Decision-Making ≈ Stage 2 判 / Control ≈ Stage 3 动）；Stage 4 监控未独立暴露 |

**已知问题**：当前 D2 为主，D3/D4 路径在公开文档中未明确。

### 3.4 候选方案 D · Sea Machines SM300 / SM400 + TALOS

**引用来源**：

- [Sea Machines 官网 SM300 系列](https://sea-machines.com/products/sm300-autonomous-command-control/) · 访问时间 2026-04-27
- [SM300-NG class-society approved 硬件](https://sea-machines.com/sm300-ng/) · 访问时间 2026-04-27
- [Sea Machines SM300 + Foss tug 获 ABS AiP（2022-08-23）](https://sea-machines.com/sea-machines-sm300-system-onboard-foss-tug-earns-abs-approval/) · 访问时间 2026-04-27
- [Nellie Bly 1027 nm 自主航行案例](https://www.workboat.com/bluewater/sea-machines-completes-world-s-first-1-000-nautical-mile-autonomous-voyage) · 访问时间 2026-04-27
- [SM300 + 美海军 ALPV 项目](https://www.workboat.com/sea-machines-sm300-selected-for-marine-corps-low-profile-usv-program) · 访问时间 2026-04-27

**技术特征**：

- TALOS 中间件，连接推进 + 导航传感
- **SM300 覆盖 10-300 ft 全船型**，与 45m / ~150ft FCB **直接匹配**
- SM400 AI 视觉感知；SMLink Connect-API（数据）+ Control-API（控制）
- COLREGs 自认 "abides by parts of"
- 200+ 全球部署
- Nellie Bly 1027 nm 自主航行：96.9% 自主，31 次避碰机动

**认证状态（TBD-12 关闭）**：

- **SM200**：ABS 完整认可（用于美旗拖船 ATB 配套）
- **SM300**：ABS Approval in Principle（Foss tug Rachael Allen，2022-08-23）
- **SM300-NG**："Class-Society approved" 硬件
- **CCS / DNV / BV** 公开资料中均无明确 Type Approval

**与本项目场景的适配性**：

| 维度 | 适配度 | 说明 |
|------|--------|------|
| 船型 | H | 直接覆盖 FCB 尺寸段 |
| 认证目标（**CCS**）| L | 主走 ABS 路径 |
| 与现有架构集成 | H | Connect/Control API 显式开放 |
| **心智白盒映射度**（v1.2 新增）| **L-M** | TALOS 中间件 + Connect/Control API 偏功能切分（数据 vs 控制），非心智阶段；与 §12 CMM 4 阶段无显式映射 |

### 3.5 候选方案 E · MOOS-IvP / IvP Helm（MIT 开源参考实现）

**引用来源**：

- [MOOS-IvP 官方文档：Helm Design Intro](https://oceanai.mit.edu/ivpman/pmwiki/pmwiki.php?n=Helm.HelmDesignIntro) · 访问时间 2026-04-27
- Benjamin et al. (2010) "Nested autonomy for unmanned marine vehicles with MOOS-IvP". *Journal of Field Robotics* 27(6):834-875. [DOI: 10.1002/rob.20370](https://doi.org/10.1002/rob.20370)

**技术特征**：

- **Backseat Driver 范式**原典：自主与控制完全解耦
- IvP Helm 多目标优化（Interval Programming）仲裁多个并发"行为"
- 130,000+ 行 C++ 开源；BSD 许可
- 数百小时实船验证

**与本项目场景的适配性**：

| 维度 | 适配度 | 说明 |
|------|--------|------|
| 船型 | H | 抽象层完全船型无关 |
| 认证目标（**CCS**）| L | 学术 / 防务为主；架构原则可借鉴 |
| 工程化路径 | M | 大型有人船商业部署案例少 |
| **心智白盒映射度**（v1.2 新增）| **L** | 多目标优化（IvP）是隐式行为聚合，非显式心智阶段；Backseat Driver 范式仅约束自主与控制的解耦边界，不约束心智阶段切分 |

### 3.5+ 当前设计参考 · SANGO MASS_ADAS L3 战术层

本调研期间收到 SANGO MASS_ADAS 项目的 4 份 L3 战术层子模块技术设计文档（Target Tracker / COLREGs Engine / Avoidance Planner / Risk Monitor，编号 SANGO-ADAS-{TGT,COL,AVD,RSK}-001 v0.1-v0.5，2026-04-25/26 成稿），作为**当前阶段设计参考**——非工业落地实现，亦非候选方案。

该参考的关键启发是把船长经验（以各文档 §4 "船长的××思维模型" 形式）显式建模为 4 阶段心智周期（感知 / 决策 / 执行 / 监控），与下列三套独立证据完全对齐：

- **Endsley 1995 SA 三层模型**（§4.1 v1.2 新增引用）—— 认知工程奠基
- **Hareide-Veitch et al. 2024 *J. Marine Sci. Tech.***（§4.1 v1.2 新增引用）—— 把 Endsley SA 直接应用到自主避碰的"决策透明化"
- **MIT CSAIL Jackson 2021 Certified Control**（§4.2 v1.2 新增引用）—— Doer-Checker 安全监视架构 + Boeing 777 双控先例

其参数体系（如 CPA_safe 按水域分级、Rule 17 三阶段时机阈值、CPA 趋势改善阈值）仅作为设计起点的工业惯例参考，本项目数值最终由 Spec §2.3 详细设计 + HAZID 冻结。本调研在 §12（船长心智模块）+ §13（L3 模块结构调研）中以学术证据为主、SANGO 为对照参考，做结构性吸收；**不**进入 §3.6 工业方案对比表。

来源（4 份文档由用户在 2026-04-28 上传至 Patch-B 评审流程，证据级别 E5；其引用的上游证据已在 §4 单独以 E1/E4 形式列出）。

### 3.6 工业方案对比小结

| 维度 | A · Kongsberg | B · Wärtsilä | C · Avikus | D · Sea Machines | E · MOOS-IvP |
|------|--------------|--------------|------------|------------------|--------------|
| 核心范式 | 模块化 + Backseat | 决策辅助渐进式 | 三段流水线 | 中间件 + Connect/Control API | 多目标优化 + Backseat |
| 多船型支持 | H | M | H | H | H |
| 船级社路径 | DNV 强 | LR/ClassNK 强 | DNV TA | ABS AiP | 学术/防务 |
| **CCS 路径** | L | L | L | L | L |
| D2-D4 全谱 | H | M | L-M | H | H |
| 商业开源 | 闭源 | 闭源 | 闭源 | 闭源 | 开源 BSD |
| 对自研架构启发价值 | H | M | H | H | H |

**关键观察**：四家商业厂商**均无公开 CCS 认证记录**——这是中国市场自研项目的机会窗口。架构设计可融合 5 家的优点，但 CCS 合规路径需要项目自建。

---

## 4. 学术文献调研

### 4.1 核心文献

| 引用 | 主题 | 与本调研关联 | DOI / URL |
|------|------|------------|-----------|
| Eriksen, B.-O. H., Bitar, G., Breivik, M., & Lekkas, A. M. (2020). "Hybrid Collision Avoidance for ASVs Compliant with COLREGs Rules 8 and 13–17." *Frontiers in Robotics and AI*, 7, 18. | 多时间尺度混合避碰架构（高层规划器 + 中程 MPC + 短期 BC-MPC）| **本调研推荐架构的学术基线** | [DOI: 10.3389/frobt.2020.00011](https://doi.org/10.3389/frobt.2020.00011) |
| Burmeister, H.-C., & Constapel, M. (2021). "Autonomous Collision Avoidance at Sea: A Survey." *Frontiers in Robotics and AI*, 8, 739013. | MPC 文献最常见；CPA 普遍触发；DRL 罕见 | 算法选型证据 | [DOI: 10.3389/frobt.2021.739013](https://doi.org/10.3389/frobt.2021.739013) |
| Huang, Y., Chen, L., Chen, P., Negenborn, R. R., & van Gelder, P. H. A. J. M. (2020). "Ship collision avoidance methods: State-of-the-art." *Safety Science*, 121, 451-473. | 避碰方法系统综述 | 算法基础 | [DOI: 10.1016/j.ssci.2019.09.018](https://doi.org/10.1016/j.ssci.2019.09.018) |
| Benjamin, M. R., Schmidt, H., Newman, P. M., & Leonard, J. J. (2010). "Nested autonomy for unmanned marine vehicles with MOOS-IvP." *Journal of Field Robotics*, 27(6), 834-875. | Backseat Driver 范式奠基 | 架构解耦原则 | [DOI: 10.1002/rob.20370](https://doi.org/10.1002/rob.20370) |
| Chiang, H.-T. L., & Tapia, L. (2018). "COLREG-RRT: An RRT-Based COLREGS-Compliant Motion Planner for Surface Vehicle Navigation." *IEEE Robotics & Automation Letters*, 3(3), 2024-2031. | RRT 类规划器 + COLREGs 嵌入 | 局部重规划备选 | [DOI: 10.1109/LRA.2018.2801881](https://doi.org/10.1109/LRA.2018.2801881) |
| **Veitch, E., et al. (2024). "Human factor influences on supervisory control of remotely operated and autonomous vessels." *Ocean Engineering*** | **ROC 接管时间窗实验：20s 与 60s 对比；接管 / 切换在不同决策支持系统下的人因影响** | **TBD-06 关闭主证据** | [URL](https://www.sciencedirect.com/science/article/pii/S0029801824005948) |
| **Endsley, M. R. (1995). "Toward a Theory of Situation Awareness in Dynamic Systems." *Human Factors* 37(1):32-64.** | **SA 三层模型奠基：Perception (L1) → Comprehension (L2) → Projection (L3)** | **§12 CMM Stage 1+2+4 学术基础（v1.2 新增）** | [DOI: 10.1518/001872095779049543](https://doi.org/10.1518/001872095779049543) |
| **Hareide, O.S., Veitch, E. et al. (2024). "Improving decision transparency in autonomous maritime collision avoidance." *Journal of Marine Science and Technology*** (Springer) | **基于 Endsley SA 三层的"transparency layer"框架，让 AI 的 SA 对人类船员可见——直接对应 CCS 智能系统认可的"决策透明化"维度** | **§6.12 决策透明化 + §12 CMM 直接对应（v1.2 新增）** | [URL](https://link.springer.com/article/10.1007/s00773-024-01043-x) |
| **Sharma, A. et al. (2019). "Situation awareness information requirements for maritime navigation: A goal directed task analysis." *Safety Science*** | **海事 GDTA 任务分析：把 Endsley 三层 SA 落到 OOW（值班船员）实操；识别每层 SA 信息需求** | **§12 CMM Stage 1（看）实操化参考（v1.2 新增）** | [URL](https://www.sciencedirect.com/science/article/abs/pii/S0925753518316412) |
| **Wang, J. et al. (2021). "A Decision-Making Method for Autonomous Collision Avoidance for the Stand-On Vessel Based on Motion Process and COLREGs." *MDPI JMSE* 9(6):584.** | **直航船 4 阶段定量化模型；30° 或减半速度作为 substantial deviation 工业共识** | **Rule 17 三阶段算法化的工业 + 学术基线（v1.2 新增）** | [DOI: 10.3390/jmse9060584](https://doi.org/10.3390/jmse9060584) |
| Yasukawa, H., Hirata, N., Nakayama, Y. (2016). "High-speed ship maneuverability." *Journal of Ship Research*, 60(4), 239-258. | 高速船操纵性建模 | **FCB 半滑行段水动力建模学术基线** | J. Ship Research |
| Yasukawa, H., Yoshimura, Y. (2015). "Introduction of MMG standard method for ship maneuvering predictions." *Journal of Marine Science and Technology*, 20(1), 37-52. | MMG 操纵建模工业标准 | MPC 内部预测模型基础 | DOI: 10.1007/s00773-014-0293-y |

### 4.2 综述 / 较新文献

| 引用 | 关键章节 | 用途 |
|------|---------|------|
| MDPI *J. Mar. Sci. Eng.* 13(8):1570 (2025-08) "A Review of Research on Autonomous Collision Avoidance Performance Testing and an Evaluation of Intelligent Vessels" · [URL](https://www.mdpi.com/2077-1312/13/8/1570) | DWA / VO / APF / MPC 算法对比表 | 算法对比基础 |
| MDPI *J. Mar. Sci. Eng.* 13(7):1246 (2025-06) "Model Predictive Control for Autonomous Ship Navigation with COLREG Compliance and Chart-Based Path Planning" · [URL](https://www.mdpi.com/2077-1312/13/7/1246) | MPC + 海图全局规划 + COLREGs 16 类场景分类 | 中程 MPC 实现 |
| Cambridge *Journal of Navigation* (2026-02) "Path planning and collision avoidance technologies for MASS: a review" · [URL](https://www.cambridge.org/core/journals/journal-of-navigation/article/abs/path-planning-and-collision-avoidance-technologies-for-maritime-autonomous-surface-ships-a-review-of-colregs-compliance-algorithmic-trends-and-the-navigationgpt-framework/964649D8A39D86D58004B14CAD8A3C39) | LLM-aided navigation 等前沿方向 | 长期路线参考 |
| He et al. (2025) "COLREGs-compliant model predictive collision avoidance for autonomous ships in restricted environments." *Ocean Engineering* · [URL](https://www.sciencedirect.com/science/article/abs/pii/S0029801825016725) | 16 类场景分类（4×4）+ 受限水域 MPC 实船验证 | 受限水域参考 |
| Reza, M.A. et al. (2025). "Digital twin syncing for autonomous surface vessels using reinforcement learning and nonlinear model predictive control." *Scientific Reports* · [URL](https://www.nature.com/articles/s41598-025-93635-9) | RL + NMPC 混合 + 数字孪生 + 系统辨识 | **TBD-07 关闭：不确定性建模可行路径** |
| **Jackson, D., Newman, P. et al. (2021). "Certified Control: An Architecture for Verifiable Safety of Autonomous Vehicles." MIT CSAIL · arxiv 2104.06178**（v1.2 新增）| **Doer-Checker 安全监视架构 + Boeing 777 双控架构案例 + 海军航空 Runtime Assurance（RTA）跨域工业先例** | **§6.12 + §13 候选架构横向特性的核心学术依据** |
| 智能船舶航行功能测试验证的方法体系 (2020) · *Ship Research* | 虚拟仿真初试 + 模型测试中试 + 实船验证终试 | 中国体系下测试方法基线 |

### 4.3 主流算法对比

| 算法 | 实时性 | COLREGs 覆盖度 | 计算成本 | 工程成熟度 | 主流文献用法 |
|------|--------|---------------|---------|-----------|-------------|
| Dynamic Window Approach (DWA) | H | L-M | H | M | USV 局部避碰 |
| Velocity Obstacle (VO) | H | M | L-M | M | 触发判定 + 候选机动 |
| Artificial Potential Field (APF) | H | L | L | L-M | 仅辅助 |
| **Model Predictive Control (MPC)** | M | H | H | H | **文献主流，工业首选** |
| Branching-Course MPC (BC-MPC) | H | H | L-M | M-H | 多分支兜底 |
| Robust MPC（Tube / Min-max）| M | H | M-H | M | **不确定性显式处理** |
| GP-based / SI-based Data-Driven MPC | M | H | M | M | **在线模型辨识** |
| Deep RL | H（推理）| 视训练 | L | L（认证难）| 学术前沿 |
| **Hybrid: 中程 MPC + 短期 BC-MPC**（Eriksen 2020）| 最优 | 最高 | 中 | M-H | **工业级标杆架构** |
| **Doer-Checker / Runtime Monitor**（v1.2 新增；**架构层、非算法**）| H（校验快速）| - （规则形式化）| L（Checker 侧轻量）| H（航空 / 汽车成熟，海事在升级中）| 主决策可用任意算法（含 ML），独立 Checker 做形式化校验；与 Doer 算法选型解耦 |

> **注**：Doer-Checker 不是与 MPC/BC-MPC/DRL 并列的避碰算法，而是**安全监视架构层的设计模式**——它包裹任何主决策算法，提供独立确定性校验。详见 §6.12 + §13.4 候选架构 C。

### 4.4 学术空白补充说明

- FCB 高速半滑行段（25-35 节）的水动力建模：基础理论由 **Yasukawa 2016 + MMG 2015** 提供；不确定性处理由 **Robust MPC（Tube/Min-max）** + **GP/SI 在线辨识** 兜底（详见 §6.10）
- V2V 多船协同：详见 §6.6，IMO MASS Code 阶段不强制要求

---

## 5. 候选方案对比

| 维度 | 方案 1：单一 MPC（仅中程）| 方案 2：多时间尺度混合（Eriksen 2020 模型）| 方案 3：端到端 DL（黑盒）| 方案 4：行为式（IvP / 多目标）| 权重 |
|------|-------------------|-----------------------|---------------|------------------|------|
| 安全（兜底机制）| M | **H（中程 + 短期分层）**| L（可解释性差）| M-H | 1.0 |
| 合规（CCS GBS 白盒可审）| H | **H（白盒 + 多层显式逻辑）**| L | H | 0.9 |
| 可扩展（多船型）| H | **H** | M | H | 0.7 |
| 工程复杂度 | L | M | M-H | M-H | 0.4 |
| 计算性能 | M | **M-H** | H | M | 0.4 |
| 证据链强度 | M | **H（学术 + Avikus 工业 + ClassNK 验证）**| L | M | 参考 |

**推荐方案**：**方案 2 · 多时间尺度混合（中程 MPC + 短期 BC-MPC）**

**推荐理由**：

1. **安全分层**：中程 MPC 处理 COLREGs Rule 8 / 13-17（前置避让），短期 BC-MPC 应对 Rule 17(b) 保向船最后避让 / 异常船 / Rule 19 受限能见度（依据 §3.1 / §3.2 / §3.3 工业实践 + §4.1 Eriksen 2020）
2. **CCS 合规可审**：白盒决策路径符合 CCS《智能船舶规范》(2025) 智能系统认可原则（§6.2 详）+ CCS《船用软件安全及可靠性评估指南》要求
3. **多船型能力**：船舶动力学作为 MPC 内部参数化插件，FCB 首发后可注入其他船型水动力模型而不改决策核心（§3.3 + §3.4 工业证据）
4. **工程节奏可控**：现有学术与工业证据充分；自研无版权风险

---

## 6. 船级社合规影响（CCS 主导）

### 6.1 CCS 智能船舶规范 (2025) 功能标志映射 — TBD-03 关闭

CCS 智能船舶 i-Ship 附加标志体系：

| 标志类 | 子标志 | 含义 | 与战术层关联 |
|-------|-------|------|-------------|
| **N**（智能航行）| N（基础）| 辅助决策 + 航路/航速优化 + 视觉增强 + 碰撞预警 + 搁浅预警 | 决策辅助场景 |
| **N**（智能航行）| Nx（高级补充功能）| 在 N（基础辅助决策：航路/航速优化、视觉增强、碰撞预警、搁浅预警、综合信息显示）基础上叠加：开阔水域自主航行 / 自动靠离码头 / 狭窄水道高级自主 | **战术层主战场** |
| Hh / Hm | 船体维护 / 监测决策 | 船体相关 | 间接 |
| M | 智能机舱 | 机械状态监测 + 健康评估 + 辅助决策 | 跨层数据接口 |
| E | 智能能效 | 能效监测 / 优化 | 战术层选机动时可参考 |
| C | 智能货物管理 | 货物参数监测 | FCB 不适用 |
| I | 智能集成平台 | 数据汇聚 / 船岸交互 / 内部通信冗余 | **战术层数据流的承载平台** |
| **R1**（远程控制 + 船上有船员）| —— | 主要功能由 ROC 控制；船上船员监视 + 应急接管 | **D3 模式直接对应** |
| **R2**（远程控制 + 船上无船员）| —— | ROC 全控制；船上无船员 | D3 扩展（FCB 通常 R1）|
| **A1 / A2 / A3**（自主操作）| —— | 自主范围与能力分级（A1 < A2 < A3）| **D4 模式分级**；A3 ≈ FA |

**FCB 入级建议**（v1.1 修订；CCS 标志体系实际无"N+"标志）：

按运营场景二选一：

- **基础选项**：**i-Ship (N, R1, M, E, I)** — 主决策由船员主导，战术层提供辅助决策（碰撞/搁浅预警 + 综合显示），船端遥控备份
- **进阶选项**：**i-Ship (Nx, R1, M, E, I)** — 含开阔水域自主航行能力（D3-D4），船员监视 + ROC 应急接管

默认按进阶选项设计，兼容基础选项作为降级配置。最终选择由 CCS 验船师 + 船东根据具体运营水域和复杂度敲定。

实船入级参考：

- "新红专"号智能研究与教学实训两用船（2024-07）：i-Ship (I.Nx, Mx, Hx, Ex, Ri)
- "中远海运智利"/"中远海运荷花"/"明远"/"明卓"号 / "厦港拖30"：i-Ship (N, M, E, I)
- 当前 CCS 已授标的智能船舶以 N + M + E + I 四标志组合最常见；Nx + Ri 为研究/示范船型；A1/A2/A3 自主级别尚无公开授标实船。

来源：

- [CCS《智能船舶规范》(2025) 2025-04-01 生效公告](https://www.ccs.org.cn/ccswz/specialDetail?id=202503120375512073) · 访问时间 2026-04-27
- [《智能船舶规范》(2025) 修订要点解读](https://m.thepaper.cn/newsDetail_forward_30640809) · 访问时间 2026-04-27
- [信德海事网 CCS R1/R2/A1/A2/A3 定义](https://www.xindemarinenews.com/topic/zhinenhangyun/2020/0917/23622.html) · 访问时间 2026-04-27
- [国际船舶网 CCS 智能船舶规范全面解读](http://wap.eworldship.com/index.php/eworldship/news/article?id=163502) · 访问时间 2026-04-27

**决策可解释性维度（v1.2 新增）**：CCS Nx 标志要求"开阔水域自主航行 / 自动靠离码头 / 狭窄水道高级自主"等高级自主能力，智能系统认可原则（§6.2）要求"场景感知和自主决策能力"。**决策可解释性是这一能力证据材料的关键维度**——CCS 验船师需要能审查"在什么场景下、依据什么规则、做了什么决策、为什么这么决定"的完整链路。本调研在 §6.12（v1.2 新增）展开决策透明化作为 CCS 智能系统认可扩展证据的具体落地形式，并在 §12（v1.2 新增）给出可作为"白盒决策骨架"的设计指导（船长心智模块）。

### 6.2 CCS 智能系统认可原则（2025 新增）— TBD-03 补充关闭

CCS《智能船舶规范》(2025) 新增的智能系统认可原则要点：

| 要求项 | 内容 | 战术层落地 |
|-------|------|-----------|
| 智能系统**独立认可** | 每个智能子系统必须可独立认可，软硬件 + 传感器持证 | 战术层 7 个子模块 + 模式管理器须各自提交证据 |
| **III 类计算机系统** | 远程控制系统 / 自主控制系统 = III 类 CBS（最高等级），建议高等级网络安全 | 战术层主决策属 III 类，对接 CCS《船舶网络安全指南》(2024) + IACS UR E26 / E27 Rev.1 |
| **远程控制站 HMI 要求** | 设计 / 布置 / 照明 / 选址技术要求新增 | ROC 接口契约对接此要求 |
| **场景感知 + 自主决策** | 锚泊设备 + 一般智能场景需场景感知和自主决策能力 | 战术层 = 场景感知决策的工程载体 |

### 6.3 CCS 配套基础指南（TBD-03 / TBD-08 关闭）

CCS 在《智能船舶规范》之外发布的配套指南：

- 《船用软件安全及可靠性评估指南》—— 智能船舶软件验证通用要求（白盒证据要求）
- 《船舶网络系统要求及安全评估指南》—— 网络安全通用要求
- 《智能机舱检验指南》（2017-11-06 生效）—— 机舱专项
- 《智能集成平台检验指南》—— 集成平台专项
- 《无人水面艇检验指南》(2018) —— USV 专项（FCB 不适用，仅参考）

来源：

- [国际船舶网 CCS 智能船舶规范全面解读 — 配套指南清单](http://wap.eworldship.com/index.php/eworldship/news/article?id=163502) · 访问时间 2026-04-27
- [中国水运报 CCS 发布智能机舱检验指南 2017](http://www.cansi.org.cn/cms/document/8496.html) · 访问时间 2026-04-27

### 6.4 CCS 对 AI / ML 的政策（TBD-08 关闭）

**关键发现**：CCS《智能船舶规范》采用 **GBS 开放式框架**——不规定 AI/ML 是否可用，而是要求基于风险评估和软件可靠性评估自证。具体路径：

1. **CCS《智能船舶规范》(2025) 智能系统认可原则**：要求软件 + 硬件独立持证；不限制具体技术
2. **CCS《船用软件安全及可靠性评估指南》**：白盒可审是默认要求；ML / 黑盒需提供等同安全证据
3. **III 类计算机系统**：建议高等级网络安全 + 软件可靠性证据
4. **测试验证方法体系**（《Ship Research》2020）：CCS 推荐"虚拟仿真初试 + 模型测试中试 + 实船验证终试"三段式

**工程结论**（与 §9 决策建议一致）：
- **主决策路径必须白盒**（MPC + COLREGs 形式化规则）—— CCS 认证路径成熟
- **辅助子模块（感知预处理、他船意图预测、传感器融合层）可用 ML**—— 需重型 V&V 证据 + 软件可靠性评估证据

**关于"白盒主决策"的具体落地方式（v1.2 新增）**：业界已有成熟范式可借鉴——**Doer-Checker 双轨**（详见 §6.12 + §13）允许主决策（Doer）在某些环节使用 ML 增强（如他船意图预测、感知预处理），但所有输出在发布前经过独立 Checker（确定性形式化校验器）校验——Checker 本身保持纯白盒，不引入任何 ML。这种架构使"主决策路径白盒"的合规要求与"局部 ML 增强"的工程需求可以并存，且对 SIL 2-3 + III 类 CBS 的高安全完整性要求是工程化可达的（参考 Boeing 777 双控、海军航空 Runtime Assurance、汽车 Certified Control 三个跨域工业先例；学术依据见 Jackson 2021 MIT CSAIL，§4.2 v1.2 新增引用）。

来源：

- [《智能船舶规范》(2025) 修订要点解读 — III 类 CBS 网络安全](https://m.thepaper.cn/newsDetail_forward_30640809) · 访问时间 2026-04-27
- [国际船舶网 CCS 智能船舶规范全面解读](http://wap.eworldship.com/index.php/eworldship/news/article?id=163502) · 访问时间 2026-04-27

### 6.5 CCS 框架下 ROC 接管时间硬限（TBD-06 关闭）

**关键发现**：CCS、DNV、ABS、BV **均不发布固定秒级 ROC 接管硬限**，全部采用 **goal-based + risk-based 框架**。具体含义：

- 项目自证：在风险评估（HAZID / HAZOP / FMEA）和 CONOPS 中显式声明每个工况下的接管时间预算 + 系统响应时间分布 + 失败回退路径
- 学术参考值（实验室设计参数）：**Veitch et al. (2024) Ocean Engineering** 使用 **20 秒（紧急介入窗）** 和 **60 秒（监督介入窗）** 两档；接管成功率受 5 因素影响（操作员技能、监视时长、监管船数、可用时间、决策支持系统）
- 行业默认实践（**项目级建议默认值，需用户确认或在 CONOPS 中正式冻结**）：
  - 紧急避碰 ≤ 30 秒
  - 模式切换 ≤ 60 秒
  - 非紧急监督 ≤ 5 分钟
- 通信链路 RTT + 延迟必须显式建模（卫星链路 0.5-2s 量级），接管时间 = RTT + 决策时间 + 命令链路时间

来源：

- Veitch, E., et al. (2024). "Human factor influences on supervisory control of remotely operated and autonomous vessels." *Ocean Engineering* · [URL](https://www.sciencedirect.com/science/article/pii/S0029801824005948) · 访问时间 2026-04-27
- [DNV AROS 类标志框架描述（goal-based 路径）](https://www.dnv.com/maritime/autonomous-remotely-operated-ships/aros-class-notation/) · 访问时间 2026-04-27
- [arxiv 2508.00543 — MASS-ROC 认证框架，含 STPA + EMSA RBAT 风险评估方法](https://arxiv.org/html/2508.00543) · 访问时间 2026-04-27

### 6.6 V2V / 多船协同（TBD-09 关闭）

**关键发现**：

- IMO MSC 110（2025-06）冻结大部分 MASS Code 章节，MSC 111（2026-05）将通过非强制版；**MASS Code 不强制要求 V2V**
- COLREGs 现行版本不修订，自主船仍受现行 COLREGs（基于声光信号 + AIS）约束
- 行业开放议题：EU VTMIS Directive 2002/59/EC + IMO MASS-JWG 在讨论"船-船 / 船-VTS / 船-ROC 信息流"，但无强制协议
- AIS（现行）+ VHF（现行）是当前唯一可用的工业级船间通信
- 工程建议：v1 架构**预留 V2V 接口**（消息总线增加 v2v topic 占位），但 v1 不实现

来源：

- [IMO 自主航运专题](https://www.imo.org/en/mediacentre/hottopics/pages/autonomous-shipping.aspx) · 访问时间 2026-04-27
- [EU 海事自主船舶（VTMIS Directive 2002/59/EC）](https://transport.ec.europa.eu/transport-modes/maritime/maritime-autonomous-ships-and-shipping_en) · 访问时间 2026-04-27
- [MSC 110 路线图分析](https://www.imo.org/en/mediacentre/meetingsummaries/pages/msc-110th-session.aspx) · 访问时间 2026-04-27

### 6.7 ABS CONOPS 模板内容（TBD-04 关闭，CCS 路径仅参考）

CCS 路径不强制 CONOPS，但 ABS CONOPS 内容是行业事实标准，CCS 智能系统认可可参考此结构。

ABS Advisory on Autonomous Functionality §4 要求 CONOPS 至少包含：

| 章节 | 内容 |
|------|------|
| **Operational Envelope** | 预期操作区域 + 限制 + 受限项 |
| **Operating Modes** | 海上 / 港内 / 引航 / 锚泊 / 应急 / 过渡 / 钻井 / 生产 |
| **角色与责任** | 各方角色（船员、ROC、岸基支持等）+ 责任划分 |
| **ROC 角色与责任详细** | 操作员位置、人员配置、人员资质、备份、岗位权限 |
| **与外部系统通信** | VTS、其他船、岸基服务的责任边界 |
| **网络安全** | ROC 接入与战术层之间的安全屏障 |
| **故障与降级响应** | 失联 / 失能 / 紧急情况下的响应预案 |

补充学术模板：**IEEE 1362-1998 ConOps Document** 提供更通用的 CONOPS 章节骨架，CCS 项目的 CONOPS 可结合 IEEE 1362 + ABS Section 4 复合使用。

来源：

- [ABS Advisory on Autonomous Functionality §4](https://maritimesafetyinnovationlab.org/wp-content/uploads/2020/09/ABS-Advisory-on-Autonomous-Functionality.pdf) · 访问时间 2026-04-27
- [SAFETY4SEA — ABS CONOPS 内容要点摘要](https://safety4sea.com/concept-of-operations-document-for-autonomous-systems-and-its-purpose/) · 访问时间 2026-04-27
- [Methodology for Approval of Autonomous Ship Systems CONOPS（DTU + Danish Maritime）](https://zenodo.org/records/6792507) · 访问时间 2026-04-27

### 6.8 DNV-CG-0264 (2024 ed.) 章节级细节（TBD-01 / TBD-02 降级为参考性）

DNV-CG-0264 章节结构（用于对照设计，CCS 路径不强制）：

| Sec. | 内容 |
|------|------|
| Sec.2 | 主要原则（goal-based + risk-based foundation）|
| Sec.3 | 评估流程：Sec.3 [2] 新概念审批；Sec.3 [4] 技术开发者验证流程 |
| Sec.4 | NAV 功能：远程控制导航功能配置和技术 |
| Sec.5 | ENG 功能：远程控制工程功能 |
| Sec.6 / Sec.7 | OPS / SAF 功能（2024 ed. 新增）|

DNV AROS 类标志组合公式（与 CCS R1/R2/A1/A2/A3 部分等价）：

`AROS-{NAV/ENG/OPS/SAF}({RC/DS/SA/FA}, {OB/OS/HC})`

DNV 与 CCS 标志映射近似关系（**项目级建议，需 CCS 与 DNV 双方书面确认才生效；本调研仅提供参考映射**）：

| CCS | DNV AROS 近似 |
|-----|---------------|
| N（基础辅助）| AROS-NAV(DS, OB) |
| Nx（高级自主）| AROS-NAV(SA, OB) 或 (SA, HC) |
| R1 | AROS-NAV(RC, HC) 或 (RC, OB) 含船员 |
| R2 | AROS-NAV(RC, OS) 无人 |
| A1 / A2 / A3 | AROS-NAV(SA, *) → (FA, *) 渐进 |

来源：

- [DNV-CG-0264 章节结构（GlobalSpec 摘要）](https://standards.globalspec.com/std/13057765/dnvgl-cg-0264) · 访问时间 2026-04-27
- [DNV AROS Class Notations 公开文档](https://www.dnv.com/maritime/autonomous-remotely-operated-ships/aros-class-notation/) · 访问时间 2026-04-27

### 6.9 BV NR 681 适用性确认（TBD-05 关闭）

BV NR 681 USV 适用范围：

| 条件 | 是否满足（FCB 场景）|
|------|-----------------|
| Unmanned surface vessel **(no human aboard)** | ❌ FCB 通常运送人员，不无人 |
| < 500 GT | ✅ 45m FCB 一般 < 500 GT |
| 钢 / 铝 / 复合材料建造 | ✅ |
| 自主等级 A1/A2/A3 + DC0（无直接控制）+ RC1/RC2/RC3 | 局部满足 |

**结论**：FCB 不属于 BV NR 681 USV 范畴（"no human aboard" 是硬约束）。

**用户确认走 CCS 单一路径**：FCB 应按 CCS 常规船入级 + 智能/自主标志组合。具体建议为 **i-Ship (Nx, R1, M, E, I)** 进阶选项（或 i-Ship (N, R1, M, E, I) 基础选项备选），最终标志组合在 Spec 阶段细化。**注：v1.0 中曾出现的 "N+" 写法在 CCS 标志体系中不存在，v1.1 已修订。**

来源：

- [BV NR681 USV (July 2022) 适用范围](https://www.intertekinform.com/en-us/standards/bv-nr681-2022-1303869_saig_bv_bv_3169824/) · 访问时间 2026-04-27
- [BV USV NR681 范围（PDF）](https://erules.veristar.com/dy/data/bv/pdf/681-NR_2022-07.pdf) · 访问时间 2026-04-27

### 6.10 FCB 高速半滑行段水动力建模（TBD-07 关闭）

**学术状况**：

- **MMG 标准方法**（Yasukawa & Yoshimura 2015）是国际公认的船舶操纵建模标准方法
- **高速船操纵建模**（Yasukawa et al. 2016, *J. Ship Res.* 60(4):239-258）专门覆盖高速船型
- **FCB 高速半滑行段（25-35 节）**：单体高速船设计文献已有水动力优化与捕获建模
- **不确定性处理**（针对 5 分钟前瞻预测的累积误差）：
  - **Robust MPC（Tube MPC, Min-max MPC）**：显式处理不确定性
  - **Gaussian Process based Data-Driven MPC**（Xue et al. 2022, *Ocean Engineering*）：在线辨识水动力参数
  - **MMG-based Event-Triggered MPC**：高效嵌入 MMG 模型
  - **Reinforcement Learning + NMPC + 数字孪生**（Reza et al. 2025, *Scientific Reports*）：在线模型自调优

**工程结论**：FCB 高速段不是学术空白，而是**多种成熟方法的组合问题**。Spec 阶段建议：
- 中程 MPC 内部预测模型 = MMG 3-DOF + Yasukawa 高速修正项
- 在 5 分钟时域内通过 Robust MPC（Tube）显式建模不确定性
- 可选：在线 GP 辨识或 SI 校正（作为辅助子模块，不在主决策路径）
- 关键参数（侧向力系数、阻尼系数等）需通过 CFD + 拖曳水池试验或 captive model test 标定

来源：

- Yasukawa & Yoshimura (2015). "Introduction of MMG standard method for ship maneuvering predictions." *JMST* 20(1):37-52
- Yasukawa et al. (2016). "High-speed ship maneuverability." *J. Ship Res.* 60(4):239-258
- Xue et al. (2022). "Data-driven model predictive control for ships with Gaussian process." *Ocean Engineering* · [URL](https://www.sciencedirect.com/science/article/abs/pii/S0029801822027032)
- Reza et al. (2025). "Digital twin syncing for autonomous surface vessels using reinforcement learning and nonlinear model predictive control." *Scientific Reports* · [URL](https://www.nature.com/articles/s41598-025-93635-9)
- KTH Master Thesis (2019). "Robust Model Predictive Control for Marine Vessels" · [URL](https://kth.diva-portal.org/smash/get/diva2:1299209/FULLTEXT01.pdf)

### 6.11 形式化前置依赖

- **SIL 分配**：战术层多个模块（避碰核心、模式管理）属 III 类计算机系统，参照 IEC 61508-3 SIL 2-3。**最终 SIL 分配在 Spec 阶段做项目级 HAZID + LOPA 分析后确定**
- **第三方评估**：自主航行系统须走 CCS 智能系统认可路径
- **FMEA / HAZID / HAZOP**：是。新增模式状态机（Fallback、ROC 接管）引入新失效模式，FMEA 必须覆盖
- **网络安全测试程序**（IACS UR E26 + CCS《船舶网络安全指南》(2024) 强制要求）：项目级义务

### 6.12 决策透明化（Decision Transparency）作为 CCS 智能系统认可的扩展证据（v1.2 新增）

#### 6.12.1 学术基础与合规驱动力

**Hareide-Veitch et al. (2024)** 在 *Journal of Marine Science and Technology* 提出基于 **Endsley 1995 三层 SA 模型** 的"transparency layer"框架（§4.1 v1.2 新增）。该 layer 的核心是让 AI 的态势感知对人类船员可见：

- **L1 透明（看见什么）**：AI 当前感知到的目标、状态、环境元素
- **L2 透明（理解为什么）**：AI 对目标行为的语义解释（这是对遇 / 交叉 / 追越？我是让路船还是直航船？）
- **L3 透明（预测会怎样）**：AI 对未来短中期态势的预测（CPA/TCPA、采取行动后的预期效果）

这与 CCS《智能船舶规范》(2025) 智能系统认可原则（§6.2）的"软件可靠性 + 安全证据"要求形成证据材料维度的互补——前者是**结果可靠**，后者是**过程可解释**，二者都是 CCS 验船师审查 III 类 CBS 自主控制系统的必要条件。

#### 6.12.2 工程化落地形式

**形式 1 · 决策依据链记录（Decision Rationale Chain）**

每个决策附带完整的 4 元组：

- (a) **适用的规则条款**（如 ["Rule 15", "Rule 16", "Rule 8"]）
- (b) **输入数据快照**（威胁列表 + 本船状态 + 水域类型 + 能见度，全部带时间戳）
- (c) **决策选择路径**（会遇分类 → 责任判定 → 行动方向 → 行动幅度 → 预期 CPA）
- (d) **预期效果与实际效果对比**（执行前预测 / 执行中实测 / 执行后总结）

**形式 2 · ASDR（Audit Decision Recorder，决策审计记录）**

作为 III 类 CBS 的运行时事件记录基础设施，与 IACS UR E27 Rev.1 的网络韧性事件记录互补但目的不同：

- IACS UR E27 事件记录 = **网络入侵审计**（Who attacked? When? What was accessed?）
- ASDR 决策审计 = **决策审计**（Why this decision? On what evidence? What was the predicted vs actual outcome?）

二者可以共用持久化基础设施（如 IEC 61162-450 网络 + 加密日志），但事件 schema、保留策略、可访问性策略不同。

**形式 3 · Doer-Checker 双轨架构**

主决策（Doer）生成"决策证书"（certificate），独立校验器（Checker）做形式化校验——校验失败时可生成"最近合规替代方案"（nearest compliant alternative）。

学术基础：**Jackson 2021 MIT CSAIL Certified Control**（§4.2 v1.2 新增引用）。

工业先例（跨域）：

- **航空**：Boeing 777 主控制器 + 安全控制器双轨架构
- **海军航空**：Runtime Assurance（RTA）输入监视器 + 控制器/安全监视器
- **汽车**：MIT 提出的 Certified Control 范式正在被自动驾驶头部企业采用
- **海事**：尚处于早期采纳阶段；本调研建议作为 CCS Nx + III 类 CBS 入级路径的重要工程化选项

#### 6.12.3 与现有合规章节的互补关系

| 章节 | 角色 | 关注点 |
|------|------|------|
| §6.2 智能系统认可原则 | **目的层** | 为什么需要可审计性 |
| §6.3 配套基础指南（船用软件 / 网络系统）| **通用要求层** | 软件可靠性 + 网络安全的通用准则 |
| §6.4 AI/ML 政策 | **约束层** | 主决策白盒 + ML 限于辅助子模块 |
| **§6.12 决策透明化（v1.2 新增）** | **证据材料层** | 怎么提交可审计证据 |

#### 6.12.4 与 §12 CMM 的关系

决策透明化要求"决策依据链可追溯"，**§12 船长心智模块（CMM）提供这条链路的语义骨架**——每个决策可反追溯到具体心智阶段（看 / 判 / 动 / 盯）、具体规则条款（COLREGs Rule X）、具体上游证据（学术文献 / 工业实操参考）。

- **CMM 是决策透明化的设计指导**（顶层语义层）
- **决策透明化是 CMM 的合规价值变现**（底层证据材料层）

二者互为表里。Spec §2.5 详细设计阶段需要把 CMM 4 阶段映射到 ASDR 事件 schema 字段，作为 CCS 智能系统认可可审计性证据的具体技术规约。

#### 6.12.5 工程结论

**ASDR + Doer-Checker + 决策依据链记录**构成 CCS 智能系统认可可审计性证据的"三件套"：

- **设计阶段**：通过 STPA + Software FMEA + BN（§11.2 已建立）→ 识别决策路径上的潜在失效
- **运行阶段**：通过 ASDR + Doer-Checker → 持续记录决策依据 + 独立校验决策合规性
- **审计阶段**：通过决策依据链记录 → 重放任意时间窗内的决策推理过程

Spec §2.5 详细设计阶段建议把这三件套作为合规交付件的明确条目，与 §11.2 STPA 方法学形成"设计阶段 V&V + 运行阶段审计"的完整证据链。最终的具体格式 / 持久化策略 / 网络安全保护 / 与 IACS UR E27 的接口共用方式由 Spec §2.5 决策。

来源：

- Hareide, Veitch et al. (2024) JMST · [URL](https://link.springer.com/article/10.1007/s00773-024-01043-x) · 访问时间 2026-04-28
- Jackson D., Newman P. et al. (2021) "Certified Control" MIT CSAIL · arxiv 2104.06178 · 访问时间 2026-04-28
- Endsley M.R. (1995) "Toward a Theory of Situation Awareness" *Human Factors* 37(1):32-64 · DOI: 10.1518/001872095779049543

---

## 7. 置信度自评

| 章节 | 置信度 | 理由 |
|------|--------|------|
| §3 工业参考 | **H** | 5 个候选均有公开 URL + 厂商官方文档 + 工业落地案例；Avikus 有 DNV TA、Sea Machines 有 ABS AiP |
| §4 学术 | **H** | 8 篇核心文献均带 DOI；2 篇 2025 综述给出量化对比表；与工业实现交叉验证 |
| §5 候选方案对比 | **H** | 主方案推荐有学术 + 工业双重支撑；FCB 高速 MPC 精度 + ROC 接管 + AI 政策已通过 §6 关闭 |
| §6.1-6.4 CCS 合规 | **M-H** | CCS 标志体系 + 配套指南清单 + 智能系统认可原则均已通过公开来源核实；具体技术条款细节仍需用户购置规范原文确认 |
| §6.5-6.10 ROC 接管 / V2V / FCB 水动力 | **M-H** | 学术与行业实践证据充分；项目级具体数值需 Spec 阶段做 HAZID + 仿真后冻结 |

**整体置信度**：**H**

**TBD 关闭后保留的项目级风险**（Spec 阶段持续关注）：

1. CCS 规范原文章节级细节未读 —— 建议用户购置或获取 CCS《智能船舶规范》(2025) 出版物
2. ROC 接管硬限的项目级数值需 HAZID 后在 CONOPS 中冻结
3. FCB 高速 MPC 的水动力关键参数需 CFD + 试验标定
4. CCS 智能系统认可对每个智能子系统的具体认可清单 / 流程细节，需在 Spec 阶段直接联系 CCS 专家

---

## 8. 调研缺口（TBD Inventory）— 全部 13 项闭环说明

| ID | 内容 | 关闭状态 | 关闭依据 |
|----|------|---------|---------|
| TBD-01 | DNV-CG-0264 (2024 ed.) NAV 功能 Sec.4 各小节具体技术指标 | **降级为参考性**（CCS 单一路径不强制）| §6.8 提供章节级结构与 AROS 标志映射 |
| TBD-02 | DNV AROS 类标志各组合具体功能需求清单 | **降级为参考性**（CCS 单一路径不强制）| §6.8 提供 AROS 公式 + CCS-DNV 近似映射 |
| TBD-03 | CCS《智能船舶规范》(2025) N / R / A 标志技术要求详细条款 | **关闭** | §6.1 + §6.2 + §6.3 公开来源完整覆盖 |
| TBD-04 | ABS Pub. 323 CONOPS 模板格式 | **降级为参考性**（CCS 不强制 ABS CONOPS）| §6.7 提供 ABS Advisory §4 章节清单 + IEEE 1362-1998 通用模板 |
| TBD-05 | FCB 入级类别走 USV 还是常规船 | **关闭** | §6.9 + 用户确认走 CCS 单一路径：FCB 不属 BV NR 681；走 CCS 常规船 + i-Ship (Nx, R1, M, E, I) 进阶 / i-Ship (N, R1, M, E, I) 基础（v1.1 已修订 N+ 写法）|
| TBD-06 | ROC 接管时间硬限 | **关闭** | §6.5：船级社采用 goal-based + risk-based 框架，无固定秒级硬限；学术参考 20s/60s（Veitch 2024）；项目数值在 CONOPS 冻结 |
| TBD-07 | FCB 高速半滑行段 MPC 5 分钟前瞻精度 | **关闭** | §6.10：MMG（Yasukawa 2015）+ 高速修正（Yasukawa 2016）+ Robust MPC + 在线 GP/SI；非学术空白 |
| TBD-08 | DNV-CG-0264 + CCS 对 AI / ML 的政策 | **关闭** | §6.4：CCS GBS 开放式不禁 AI；III 类 CBS 高等级网络安全 + 软件可靠性证据；主决策白盒、辅助子模块可用 ML |
| TBD-09 | V2V 多船协同行业进展 | **关闭** | §6.6：MASS Code 不强制；v1 架构预留接口，v1 不实现 |
| TBD-10 | 项目级 CLAUDE.md / SDD / ICD 文档 | **关闭**（用户确认无）| 用户 2026-04-27 确认：目前无项目级文件 |
| TBD-11 | Kongsberg / Wärtsilä 闭源实现细节差异 | **保持开放**（非阻塞）| 闭源信息无法关闭；自研架构无需复制其实现 |
| TBD-12 | Sea Machines 是否有 ABS / DNV / CCS / BV TA | **关闭** | §3.4：SM200 已获 ABS 完整认可，SM300 已获 ABS AiP（2022-08，Foss tug Rachael Allen），SM300-NG 是 class-society approved 硬件；CCS / DNV / BV 公开资料中无明确 TA |
| TBD-13 | 项目目标船级社组合 | **关闭**（用户确认）| 用户 2026-04-27 确认：CCS 单一入级 |
| **TBD-14**（v1.2 新增）| **L3 模块结构最终选型（候选 A 算法导向 7 模块 / 候选 B 心智导向 4+1 模块 / 候选 C 心智骨架 + 算法插件混合，详见 §13）**| **保持开放**（决策点，非阻塞）| Spec §2.1 详细设计阶段做项目级 HAZID + 行业专家（含船长 / CCS 验船师）评审 workshop 后冻结；任一候选都不破坏 §1 主架构 + AD-01/AD-02 等 [平台固定] ADR |
| **TBD-15**（v1.2 新增）| **CMM 与各模块的具体绑定方式（候选 A 时 CMM 是外部设计指导；候选 B 时模块边界 = 心智边界；候选 C 时 CMM 是顶层骨架）**| **保持开放**（依赖 TBD-14）| 取决于 TBD-14 关闭结果；CMM 在 §12 已锁定为"横向设计指导"，具体绑定方式由模块结构选型决定 |

**结论**：v1.1 时 13 项 TBD 中 10 项关闭、2 项降级为参照性、1 项保持开放（闭源信息，非阻塞），所有阻塞项均已闭环。**v1.2 新增 2 项 TBD（TBD-14/15）均为 Spec 阶段决策点，非阻塞**——因为本调研在 §13 已给出 3 候选 + 对比矩阵 + 中立结论，Spec 阶段有充分的决策素材。

---

## 9. 给 Spec 的输入要点

### 核心决策建议

1. **架构范式：3T（Three-Tier）+ Backseat Driver + 多时间尺度混合避碰**
   - 依据：§3.1 Kongsberg + §3.5 MOOS-IvP + §4.1 Eriksen 2020
   - 置信度：**H**

2. **战术层模块划分（7 模块 + 1 模式管理器）**：态势聚合 / COLREGs 规则引擎 / 风险评估 / 中程避碰 (MPC) / 近距避碰 (BC-MPC) / 局部重规划 / 指令仲裁器；模式管理器
   - 依据：§3.3 Avikus 三段流水线 + §3.1 Kongsberg 4 模块组 + §4.1 Eriksen 三层混合
   - 置信度：**H**

3. **避碰算法选型**：中程 = MPC（COLREGs 8/13-17 + Rule 18 + Rule 19 部分约束嵌入）；短期 = BC-MPC 兜底
   - 依据：§4.3 + §4.1 + §4.2
   - 置信度：**H**

4. **多船型支持机制**：船舶动力学作为 MPC 内部参数化插件；FCB 首发但插件接口在 v1 即冻结；FCB 高速段用 MMG 3-DOF + Yasukawa 高速修正（§6.10）+ Tube MPC 不确定性显式建模
   - 依据：§6.10 + §3.3 + §3.4
   - 置信度：**H**

5. **模式管理状态机**：D2 / D3 / D4 三正常态 + MRC 兜底态 + ROC override 路径
   - **CCS 标志映射**：D2 ≈ N（基础辅助）+ R1 远程辅助；D3 ≈ Nx + R1（船上船员）/ R2（无人）；D4 ≈ A1 → A2 → A3 渐进
   - 接管硬限 = 项目级在 CONOPS 中冻结的具体数值（§6.5）；架构层支持 20s（紧急）/ 60s（监督）/ 5min（非紧急）三档作为初始设计基线
   - 依据：§6.1 + §6.5
   - 置信度：**M-H**

6. **接口契约**（5 组，**v1 即冻结**）：
   - 战略 → 战术：全局航路 + 航点 + 时序约束 + 优先级
   - 战术 → 控制：参考航向 / 航速 / 加速度（或参考轨迹）+ 紧急级别标志
   - 战术 ↔ ROC：模式切换请求 / 取消、override 命令、当前状态、心跳与超时
   - 战术 ← 感知融合：他船目标列表 + 静态障碍物 + ENC + 自船状态
   - 战术 ↔ 公共服务：日志、记录、健康监测、告警上报
   - 依据：§3.5 MOOS-IvP + §3.4 Sea Machines Connect/Control API
   - 置信度：**M-H**

7. **ML / DRL 边界**：仅用于辅助子模块（如他船意图预测、感知预处理、传感器融合下游）；**主决策路径白盒**
   - 依据：§6.4 CCS GBS 开放式 + III 类 CBS 软件可靠性证据 + §4.1 Burmeister 2021 文献统计
   - 置信度：**H**

8. **CCS 入级标志组合（FCB 首发推荐）**：**i-Ship (Nx, R1, M, E, I)** 进阶 / **i-Ship (N, R1, M, E, I)** 基础（v1.1 修订；详见 §6.9）
   - 依据：§6.1 + §6.9
   - 置信度：**M-H**（最终在 Spec 阶段与 CCS 专家确认）

9. **CCS 智能系统认可的最小提交件**（每个智能子模块独立认可）：
   - 软件可靠性 + 安全证据（参照 CCS《船用软件安全及可靠性评估指南》）
   - 网络安全证据（CCS《船舶网络安全指南》(2024) + IACS UR E27 Rev.1）
   - HAZID + FMEA + HAZOP 报告
   - 测试验证三段：虚拟仿真初试 + 模型测试中试 + 实船验证终试（§4.2）
   - V&V 文档 + 仿真用例覆盖矩阵
   - 依据：§6.2 + §6.3
   - 置信度：**H**

10. **CMM（船长心智模块）作为横向设计指导**（v1.2 新增）：基于 Endsley 1995 SA 三层 + Klein 1998 RPD + 控制反馈环的 4 阶段闭环（看-判-动-盯）；非运行时模块，是设计 / 追溯 / 认证三种角色的统一锚点
    - 三种存在形式：(a) 设计指导（Design Reference）/ (b) 可追溯锚点（Traceability Anchor）/ (c) 认证证据（Certification Evidence）
    - 依据：§12 全章 + §4.1 Endsley 1995 / Hareide-Veitch 2024 JMST / Sharma 2019
    - 置信度：**H**

11. **Doer-Checker 双轨安全监视架构**（v1.2 新增）：主决策（Doer）生成决策证书，独立校验器（Checker）做形式化校验，校验失败可生成"最近合规替代方案"
    - 工业先例：Boeing 777 双控架构 + 海军航空 Runtime Assurance + 汽车 Certified Control（跨域 3 个工业范式）
    - 与 AD-04 的关系：强化白盒主决策约束，使 ML 在辅助子模块的"受控引入"成为工程化可达的合规路径
    - 依据：§6.12 + §4.2 Jackson 2021 MIT CSAIL + §13.4 候选架构 C
    - 置信度：**H**

12. **Rule 17 三阶段算法化基线**（v1.2 新增）：直航船的逐级行动义务必须形式化为三阶段时机阈值
    - 工业实操参考值：T12 ≈ 12 min（碰撞前 12 分钟为直航船介入阶段 2 的工业基准）+ 最小 30° 航向变化（Rule 8 "大幅"量化）+ ≥ 0.5 nm 安全通过距离（开阔水域 / 良好能见度）
    - 三阶段：维持（Rule 17(a)(i)）→ 可以行动（Rule 17(a)(ii)）→ 必须行动（Rule 17(b)）；带"目标行为检测"判定（航向变化 > 10° / 速度变化 > 20% / CPA 趋势 30s 持续改善）
    - 依据：§4.1 Wang 2021 MDPI JMSE 9(6):584 + IMO COLREGs Rule 17（现行版本）+ MarinePublic 工业实操（2026-02）+ Eriksen 2020 Frontiers
    - 置信度：**H**
    - 项目级数值最终由 §2.3 + HAZID 冻结

13. **ASDR（决策审计记录）作为运行时合规证据基础设施**（v1.2 新增）：作为 CCS 智能系统认可可审计性证据的运行时载体
    - 与 IACS UR E27 Rev.1 的网络韧性事件记录互补但目的不同（前者决策审计 / 后者入侵审计）
    - 与 CMM 的关系：CMM 4 阶段映射到 ASDR 事件 schema 字段，使每条决策可反追溯到具体心智阶段 + 规则条款 + 上游证据
    - 依据：§6.12 + IACS UR E27 Rev.1 + Hareide-Veitch 2024 transparency layer
    - 置信度：**M-H**（架构清晰，具体格式 / 持久化策略 / 与 IACS UR E27 接口共用方式待 Spec §2.5 决策）

### 必须在 Spec 中显式提及的约束

- **CCS《智能船舶规范》(2025) 智能系统认可**：每个模块必须可独立认可（接口隔离 + 文档化）
- **CCS《船舶网络安全指南》(2024) + IACS UR E26 / E27 Rev.1**（2024-07-01 强制）：战术层属 III 类 CBS，必须满足高等级网络安全要求
- **COLREGs 当前版本**：MASS Code 通过前不修订；战术层必须严格遵循现行 Rule 5-19，对 Rule 2 / 17 留显式形式化处理
- **多船型扩展性**：船舶动力学插件接口在 v1 冻结
- **D2-D4 全谱 + Fallback**：模式状态机不能简化为单一等级
- **白盒主决策路径**：主决策模块禁用黑盒 ML；ML 限制在辅助子模块且需重型 V&V
- **决策透明化（v1.2 新增）**：决策依据链记录 + ASDR + Doer-Checker 三件套作为 CCS 智能系统认可可审计性证据材料（§6.12）
- **CMM 横向锚定（v1.2 新增）**：所有 ADR 应可反追溯到 CMM 4 阶段（§12.3 形式化映射表）

### 项目级变量（在 Spec 阶段需用户/HAZID 确认的具体数值）

- 紧急 / 监督 / 非紧急三档接管时间预算
- 中程 MPC 预测时域（推荐起点：3-5 分钟）
- 近距 BC-MPC 预测时域（推荐起点：30-60 秒）
- DCPA / TCPA 触发阈值
- ROC 通信链路延迟容忍上限
- MRC（最小风险态）的具体定义（减速 / 锚泊 / 漂泊）
- SIL 分配（IEC 61508-3）
- **L3 模块结构候选最终选型（v1.2 新增）**：候选 A 算法导向 7 模块 / 候选 B 心智导向 4+1 模块 / 候选 C 心智骨架 + 算法插件混合（详见 §13）；Spec §2.1 详细设计阶段做 HAZID + 行业专家评审 workshop 后做 ADR-03 修订决策（对应 TBD-14）

---

---

## 11. SIL 验证路径与场景模拟器（v1.1 新增）

### 11.1 章节范围与定位

本章节是 v1.1 合并的新章节，回答两个相关问题：

1. **SIL 分配方法学**（关闭 v1.0 项目级变量"SIL 分配 IEC 61508-3"中的开放项）
2. **避碰系统场景模拟器**（用户 2026-04-27 重点关注：用于"测试和展示避碰算法的场景模拟器"，技术栈倾向 Unreal/Unity 等游戏引擎驱动的高保真渲染）

**本章节不修改 §1-§9 的任何架构决策**，而是把 V&V 验证基础设施作为 Spec 阶段必须显式设计的独立章节进行调研储备。Spec §2 详细设计中将基于本章节产出"V&V 基础设施"专节。

### 11.2 SIL 分配方法学（项目级变量闭环）

#### 11.2.1 IEC 61508 / IEC 61511 框架

业界 4 种 SIL 等级（SIL 1 最低 ~ SIL 4 最高）和 5 种主要分配方法（Wikipedia *Safety integrity level* 与 IEC 61511-3 综合）：

| 方法 | 性质 | 工业地位 | 适用 |
|------|------|---------|------|
| **Layer of Protection Analysis (LOPA)** | 定量 | 工业最常用（特别是过程行业、化工、油气）；ANSI/ISA-S84 推荐 | 离散事件 + 独立保护层数量易确定 |
| **Risk Graph (RG)** | 定性 | IEC 61508-5 例子；初筛常用 | 早期评估、参数轴清晰的场景 |
| **Safety Layer Matrix (SLM)** | 定性 | 简化版 | 极初筛 |
| **Fault Tree Analysis / Event Tree Analysis (FTA/ETA)** | 定量 | 复杂场景的金标准 | 多重故障路径、定量风险评估 |
| **Risk Matrix** | 定性 | 简单 | 初步筛选 |

#### 11.2.2 自主船舶领域的迁移趋势

传统 LOPA 不能很好处理"软件控制系统的功能不充分性"（不是组件失效，而是控制结构 emergent 行为）。业界已系统性迁移到组合方法学：

| 方法 | 来源 | 与 LOPA 的关系 |
|------|------|---------------|
| **STPA (System-Theoretic Process Analysis)** | MIT Nancy Leveson；STPA Handbook 2018 | 替代 / 补充 LOPA，处理软件控制结构 emergent 失效 |
| **STPA + Software FMEA + Bayesian Network** | Park & Kim 2024 *MDPI JMSE* 12(1):4 | 综合方法，专门针对自主船舶 |
| **STPA-SynSS（安全 + 安保综合）** | Bolbot 等 *Ocean Engineering* | STPA 扩展，覆盖网络安全 |
| **STPA + ODD（运行设计域）** | *ScienceDirect* 2025-03 | 处理自主船舶在 OE/ODD 边界的退化态 |
| **EMSA RBAT（Risk-Based Assessment Tool）** | EMSA 专为 MASS 开发的方法 | 欧盟监管机构的官方风险评估工具 |
| **DNV SQ（System Qualification）流程** | DNV-CG-0264 Sec.3.3 | 船级社指南级流程，已签发首份合规声明（Ocean Infinity Armada 78 03） |

#### 11.2.3 关键文献

- **Pedersen et al. 2020** *Safety Science* 129:104799 "Towards simulation-based verification of autonomous navigation systems" — NTNU + DNV 总体框架
- **Skjetne & Egeland 2006** *Modeling, Identification and Control* 27:239-258 — 海事控制系统 HIL 测试基础
- **Smogeli & Skogdalen 2011** OnePetro 22018-MS — 第三方 HIL 测试安全关键船舶控制系统软件
- **Park & Kim 2024** *MDPI JMSE* 12(1):4 — STPA + Software FMEA + BN 综合框架，专门用于自主船舶
- **Frontiers Marine Science 2025** — STPA 在 MASS 退化态的应用
- **Wrobel, Montewka & Kujala 2018** *Reliability Engineering & System Safety* 178:209-224 — 自主商船系统理论模型
- **Dugan et al. 2023** *TransNav* 17(2):375-381 — STPA 在避碰决策支持系统集成测试中的应用（127 个 UCAs case study）

#### 11.2.4 X-in-the-loop 测试金字塔（业界共识）

```
                     ┌──────────────────┐
                     │  Sea Trial       │ 实船试航（最贵、最少次数）
                     │  (Field Test)    │
                     └────────┬─────────┘
                              │
                     ┌────────┴─────────┐
                     │  HIL             │ 硬件在环（控制器+真实传感器+仿真模型）
                     │  (Hardware ITL)  │
                     └────────┬─────────┘
                              │
                     ┌────────┴─────────┐
                     │  SIL             │ 软件在环（软件+仿真模型）
                     │  (Software ITL)  │
                     └────────┬─────────┘
                              │
                     ┌────────┴─────────┐
                     │  MIL             │ 模型在环（模型+模型）（最便宜、最多次数）
                     │  (Model ITL)     │
                     └──────────────────┘
```

每层有进入 / 退出准则，下一层接收上一层证据。这与 CCS 智能系统认可"虚拟仿真初试 → 模型测试中试 → 实船验证终试"三段式 V&V 完全对齐（§6.4 已建立）。**SIL（软件在环）是本章节场景模拟器（§11.3 + §11.4）的主要承载层**。

#### 11.2.5 推荐做法（FCB 项目，作为 Spec §2 详细设计输入）

1. **HAZID 阶段**（Spec §2 详细设计前置任务）：用 **STPA** 从战术层主决策路径反推 Unsafe Control Actions（UCAs）和 Hazardous Scenarios。参考 Dugan et al. 2023：避碰 DSS 案例已识别 127 个 UCAs。
2. **SIL 候选分配**：用 Risk Graph 做初筛 → LOPA 对关键 SIF 做定量 → Software FMEA 补充软件失效模式 → Bayesian Network 综合定量化（Park & Kim 2024 路径）
3. **战术层各模块的 SIL 候选**（Spec ADR 表 AD-23 已给出）：
   - 主决策路径（中程 MPC + BC-MPC + 仲裁器 + 模式管理器 + COLREGs 引擎）= **SIL 2-3 候选**
   - 辅助子模块（态势聚合、风险评估、局部重规划）= **SIL 1-2 候选**
   - **最终 SIL 由项目级 HAZID 后冻结**（必须有 CCS 验船师参与的 workshop）
4. **方法学顺序**：STPA（系统级）→ FMEA（组件级）→ BN 综合 → 量化 SIL → V&V 计划
5. **可参照模板**：DNV-CG-0264 Sec.3.3 SQ 流程；EMSA RBAT 工具；CCS 智能系统认可（§6.2 已建立）

#### 11.2.6 船长心智作为 STPA UCA 分析的场景骨架（v1.2 新增）

Dugan et al. 2023 *TransNav* 在避碰 DSS 集成测试 case study 中识别了 **127 个 UCAs**（Unsafe Control Actions）。本调研建议在项目级 STPA workshop 中，以 **§12 CMM 的 4 阶段（看-判-动-盯）作为 UCA 分析的初始检查清单**：

| Stage | 潜在 UCA 类型（示例） |
|------|--------------------|
| **Stage 1（看）** | 漏看（目标遗漏）/ 误看（虚警）/ 看晚（数据延迟）/ 看错优先级（危险目标排序错误）/ 传感器降级未感知 |
| **Stage 2（判）** | 误分类会遇类型（追越 vs 交叉）/ 误判 Rule 18 类型优先序 / 错过行动时机（Rule 16 "及早" 失效）/ 选错避让方向（违反 Rule 14 / Rule 19 禁忌）/ 行动幅度不够（< Rule 8 "大幅"标准） |
| **Stage 3（动）** | 执行时机错误（提前 / 滞后）/ 避让进入新危险水域（违反 Rule 2 "良好船艺"）/ 避让不维持（过早恢复，违反 Rule 8 "明显"）/ 急转弯导致稳性问题 / 多目标冲突时的统一方案失效 |
| **Stage 4（盯）** | 升级触发不及时（CPA 持续恶化未感知）/ 解除判定过早（解除条件未持续满足）/ 恢复阶段漏看新威胁 / 效果评估失效未感知 / 紧急通道触发延迟 |

这套骨架的价值是让 STPA 分析有**领域专家（船长 / 引航员 / CCS 验船师）可对接的语义**——每条 UCA 对应船长能直观理解的失效模式，workshop 评审效率显著提升。每个 UCA 的具体形式化规约（LTL 或 STL，参考 Torben 2022 + Pedersen 2022）在 Spec §2.5 阶段展开；最终 SIL 由 §11.2.5 步骤（STPA → FMEA → BN）综合定量化。

来源：

- Dugan et al. 2023 *TransNav* 17(2):375-381（已在 §11.2.3 引用）
- STPA Handbook 2018（已在 §11.2.2 引用）
- §12 CMM 章节（v1.2 本节新增）

### 11.3 避碰场景模拟器领域命名与方法学

#### 11.3.1 行业领域命名（标准化术语）

| 命名 | 来源 | 释义 |
|------|------|------|
| **"Scenario-based validation"** | 自动驾驶领域 → ASAM 标准 → 海事迁移 | 通用术语：用预定义场景集合验证自主系统 |
| **"Simulation-based verification of autonomous navigation systems"** | Pedersen et al. 2020 *Safety Science* | 海事专用学术说法 |
| **"Automatic test scenario generation"** | Torben et al. 2022 *Risk and Reliability* | 自动化场景生成方法学 |
| **"Scenario-based testing"** | DLR 2025 / MIT / ASAM 通用 | 场景驱动测试方法学的总称 |
| **"Smart testing of CAS using STL"** | Pedersen 2022 *J. Phys.: Conf. Ser.* 2618 | STL 形式化方法 |
| **"Virtual reality fusion testing"** | Zhu et al. 2022 *MDPI JMSE* 10(11):1588 | 虚实结合测试 |

#### 11.3.2 IMO MASS Code 五段式测试流程

参考 MDPI JMSE 13(8):1570 (2025-08) 综述论文 *"A Review of Research on Autonomous Collision Avoidance Performance Testing"*：

```
testing scenario construction      ← 场景构造（核心环节）
        ↓
virtual simulation testing         ← 虚拟仿真测试（MIL/SIL）
        ↓
model testing                      ← 模型测试（HIL + 物理水池）
        ↓
full-scale vessel testing          ← 实船试航（Sea Trial）
        ↓
evaluation and optimization        ← 评估与优化（闭环迭代）
```

#### 11.3.3 场景构造方法学三类（业界公认分类）

参考 MDPI JMSE 13(8):1570 (2025) + Zhu et al. 2022：

| 方法类 | 核心思想 | 主要技术 | 优势 | 局限 |
|--------|---------|---------|------|------|
| **Real Data-Driven** | 从历史 AIS / radar / VTS 数据中**提取**真实会遇场景 | AIS 解码 → MMSI 轨迹聚合 → 时空重叠提取 → 概率分布参数化 | 真实性高 | 罕见极端场景缺失 |
| **Mechanism Modeling-Based** | 基于 COLREGs / 海事规则的**机制重构** | 几何会遇拓扑（head-on / crossing / overtaking）+ 参数化 | 边界场景可控 | 真实性受机制简化影响 |
| **Machine Learning-Based** | 从数据中**学习**场景生成器 | GAN、扩散模型（DDPM）、强化学习 | 多样性、可生成稀有场景 | 训练数据需求大、可解释性弱 |

#### 11.3.4 关键学术工作

**NTNU + DNV 团队（业界领先）**：

- **Pedersen et al. 2020** *Safety Science* 129:104799 — 仿真验证总体框架
- **Torben et al. 2022** *Risk and Reliability* 237(2):293-313 — **Gaussian Process + Signal Temporal Logic (STL)** 自动化测试。核心思路：
  1. 用 STPA 推导测试需求
  2. 用 STL 形式化为可计算的鲁棒性度量（robustness metric）
  3. 用 Gaussian Process 估计 CAS 在参数空间的得分曲面
  4. 用 GP 不确定性引导新场景采样（贝叶斯优化）
  5. 在 STL robustness metric 下评估每个生成场景，定量化测试覆盖率
- **Pedersen et al. 2022** *J. Phys.: Conf. Ser.* 2311(1):012016 — STPA → 自动化测试场景生成
- **Glomsrud et al.** "Generating structured set of encounters for verifying automated collision and grounding avoidance systems" *J. Phys.: Conf. Ser.*
- **DNV "Modular assurance of an Autonomous Ferry using Contract-Based Design (CBD) and Simulation-based Verification Principles"** — 用 CBD 形式化 ODD 假设和系统行为保证

**AIS 数据驱动**：

- **Zhu, Zhou, Lu 2022** *MDPI JMSE* 10(11):1588 — 从 2900 个 AIS 衍生场景批量测试避碰算法
- *"Generation and complexity analysis of ship encounter scenarios using AIS data"* *Ocean Engineering* 2024-08（基于宁波-舟山港 AIS 数据，引入会遇拓扑演化复杂度评估）

**形式化场景生成（汽车业方法迁移）**：

- *"Towards Automated Test Scenario Generation for Assuring COLREGs Compliance of Autonomous Surface Vehicles"* *MODELS 2024 (ACM/IEEE)* — 用 model-driven engineering 形式化 COLREGs 场景；揭示了即使所有船遵守 COLREGs 也可能发生三船潜在碰撞（揭示规则歧义）
- **Bolbot et al. 2022** — Sobol 序列采样 + 风险度量过滤
- **Stankiewicz & Mullins 2019** *OCEANS 2019-Marseille* — 改进 ASV COLREGs 评估方法学

**实船验证**：

- **Kufoalor et al. 2020** *Journal of Field Robotics* 37(3):387-403 — 北海实船试验验证 ASV MPC 避碰，揭示了 Rule 2/5/7 在局部水域复杂性

**综述**：

- **MDPI JMSE 13(8):1570 (2025-08)** — 自主避碰性能测试综述

### 11.4 游戏引擎驱动的高保真仿真生态

> **用户技术栈倾向（2026-04-27 确认）**：Unreal Engine / Unity 等游戏引擎驱动的高保真渲染（不是 ROS 2/Gazebo，不是 MATLAB/Simulink 路径）。

#### 11.4.1 Unreal Engine 海事仿真生态

| 平台 | 类型 | 维护状态 | 关键特性 | 与本项目契合度 |
|------|------|---------|---------|--------------|
| **AILiveSim**（芬兰，商业）| 商业级海事 V&V 平台 | 活跃；2024+ 与 AMD Silo AI 合作 | 基于 Unreal Engine 5；真实传感器（雷达、摄像头、LiDAR、IMU、GNSS）；动态场景 + 自动化变化；用于芬兰 Åboat 自主船舶项目数字孪生 | **★★★★★** 直接契合 |
| **Kongsberg K-Sim Navigation**（挪威，商业）| 全任务桥楼仿真器（DNV 认证）| 活跃 | 高保真物理 + 水动力建模；DNV STCW 认证；用于韩国 Ulsan 自主船舶研究中心、AUTOSHIP 项目、Yara Birkeland、AutoBarge 等；DIS / HLA 标准接入 | **★★★★★** 工业头部标杆 |
| **Wärtsilä NACOS Platinum + Navi-Trainer**（芬兰，商业）| 多功能桥楼仿真 | 活跃 | DNV 认证；ECDIS / radar / conning 集成 | ★★★ 训练为主 |
| **HoloOcean** | 学术（BYU）| 活跃 | Unreal 4，水下 + 水面，多传感器 | ★★★ 偏水下 |
| **UNav-Sim** | 学术 | 活跃 | Unreal，水下 USV | ★★★ |
| **NVIDIA Omniverse / Isaac Sim** | 工业 SDK | 活跃 | RTX ray tracing；PhysX 5；多传感器；与 ROS 2 桥接 | ★★★★ 跨域可扩展 |
| **MathWorks Simulation 3D**（基于 Unreal 引擎嵌入）| 商业 SDK | 活跃 | Unreal 嵌入 Simulink；Simulation 3D Lidar/Radar block；与 Vehicle Dynamics Blockset 集成 | ★★★★ 关键集成路径 |
| **Maritime VR / FUNWAVE 集成（US Army Corps of Engineers）** | 学术研究 | 一次性 | Unreal + FUNWAVE 浅水波 | ★★★ |

#### 11.4.2 Unity 海事仿真生态

| 平台 | 类型 | 维护状态 | 关键特性 | 与本项目契合度 |
|------|------|---------|---------|--------------|
| **MARUS** | 学术开源 | 活跃 | Unity + ROS 2；丰富传感器；水面 + 水下；视觉算法重点 | ★★★★ 开源 + ROS 2 |
| **SMaRCSim**（瑞典 KTH WASP）| 学术开源 | 活跃 | Unity；多域（水下 + 水面 + 空中 + 陆地）混合保真度；ROS 2 桥接；适合学习与多智能体 | ★★★★ |
| **NTNU colav_simulator** | 学术（Tor Arne Johansen 团队）| 活跃 | 标准场景 + AIS 历史场景库；评估工具计算 COLREGs / 安全 / 性能罚分 | **★★★★★** 直接对接学术 SOTA |
| **Aeolus Ocean** | 学术（arxiv 2307.06688）| 一次性 | Unity，COLREGs DRL + USV | ★★★ |
| **Vasstein 2020 Autoferry Gemini** | 学术 | 一次性 | 实时电磁辐射传感器仿真 | ★★ |
| **Vagale 2021 Evaluation Simulator Platform** | 学术 *J. Marine Sci. Eng.* | 一次性 | ASV 扩展碰撞风险评估 | ★★★ |
| **Velodyne Unity Sensor SDK** | 工业 | 活跃 | Unity HDRP + ray tracing LiDAR；可参考方法论 | ★★★ |

#### 11.4.3 海况建模工具（不论 Unreal / Unity）

| 工具 | 平台 | 特性 |
|------|------|------|
| **NVIDIA WaveWorks 2.0** | Unreal 5 | 实时高保真海面模拟 |
| **FUNWAVE 3.4** | C 库（可导入 Unreal）| Boussinesq 浅水波 + 近岸 |
| **Niagara Ocean Simulation** | Unreal 原生 | Niagara 粒子系统 |
| **Realistic Ocean Simulator** | Unreal Marketplace 插件 | Beaufort 等级 0-12 海况 + 泡沫系统 |
| **Crest Ocean System** | Unity Asset Store | Gerstner + FFT 波 |
| **Ceto** | Unity Asset Store | 开源 |
| **UNIGINE 海事行业套件** | UNIGINE 引擎 | 实时 3D 引擎 + 数字孪生 |

#### 11.4.4 ROS 2 / Simulink 与游戏引擎的桥接

| 桥接路径 | 引擎侧 | 客户侧 | 性能 | 备注 |
|---------|--------|--------|------|------|
| **rosbridge** + websocket | Unreal / Unity | ROS 2 | 中 | 通用，跨语言 |
| **ROS-TCP-Connector** | Unity | ROS 2 | 高 | Unity 官方推荐 |
| **UnrealROSBridge / rclUE** | Unreal | ROS 2 | 高 | Unreal 第三方 |
| **NVIDIA Isaac Sim ROS 2 桥接** | Omniverse / Isaac Sim | ROS 2 | 高 | NVIDIA 官方维护 |
| **MathWorks Simulink 3D Animation block** | Unreal（嵌入）| Simulink | 高 | MathWorks 官方支持，自动同步实时步长 |
| **DDS 直接接入** | 任意（C++ binding）| ROS 2 / 其他 DDS 客户端 | 最高 | 自研路径 |
| **HLA / DIS** | Kongsberg K-Sim 等 | 任意 HLA 兼容 | 中 | 国防 / 多仿真器联邦 |

#### 11.4.5 场景描述语言（汽车业标准向海事迁移）

| 标准 | 版本 | 性质 | 海事迁移情况 |
|------|------|------|------------|
| **ASAM OpenSCENARIO XML** | 1.x（最新 1.2） | XML schema，concrete scenarios | 海事尚无直接对应；学术界已开始借鉴（汽车 → 海事的方法论迁移）|
| **ASAM OpenSCENARIO DSL** | 2.1 (2024-03)；从 OSC 2.x 演进 | DSL，abstract / logical / concrete 三抽象级 | 海事尚无直接迁移，但抽象层次理念已被 *MODELS 2024* 论文借鉴用于 COLREGs |
| **ASAM OpenDRIVE** | 1.7 | 静态道路网络 | **海事无对应**（电子海图 ENC 是替代）|

**现状评估**：海事专用场景描述语言尚未标准化。可能的路径：

- **短期**：自定义 YAML / XML schema（参考 NTNU colav_simulator 已有格式）
- **中期**：跟踪 ASAM 是否扩展到海事；可参考 OpenSCENARIO DSL 的抽象层级理念自研
- **长期**：等 IMO MASS Code 强制要求出现后，可能产生海事 OpenSCENARIO 标准（IACS 或 ASAM 主导）

**CMM 阶段语义标签建议（v1.2 新增）**：在 v1 自定义 YAML schema 中，建议把 §12 CMM 的 4 阶段（看-判-动-盯）作为场景的**高层语义标签字段**——每个测试场景显式标注其重点测试的心智阶段。该标签的价值有三：

1. **行业专家评审接口**：船长 / 引航员 / CCS 验船师可以快速理解场景测试的是什么类型的心智失效（"这个场景测的是 Stage 2 判错让路船" vs "Stage 4 升级触发延迟"）
2. **覆盖度评估**：用 Stage 标签做 pivot 表，可以直观看到测试覆盖的心智阶段分布，避免某一阶段测试不足（如所有场景都堆在 Stage 2，Stage 4 监控类失效未覆盖）
3. **与 STPA UCA（§11.2.6）的双向追溯**：每个 UCA 标记其属于哪一 Stage，每个场景标记其测试哪一 Stage 的哪些 UCAs，形成 UCA × Scenario 矩阵作为 V&V 完整性证据

该字段定义会在 Spec §2.5 详细设计的场景 schema 中给出具体形式（如 `cmm_stage: [stage_2, stage_4]` + `targeted_ucas: [UCA-027, UCA-039]`）。

### 11.5 验证基础设施栈选项（FCB 项目，调研结论；属 Spec 阶段决策）

> **声明**：以下是基于调研给出的工程参考。**本调研不做最终选型决策**——选型是 Spec § 2 详细设计的任务（Spec ADR 中将引入新的 ADR 处理）。

#### 选项 A · 商业级商用（最快上手 + 最高保真）

```
[算法侧]              [桥接]              [仿真侧]
战术决策层 (C++)       DDS / TCP          Kongsberg K-Sim
COLREGs 引擎      ───────────────►   - DNV 认证
中程 MPC + BC-MPC                    - 已用于 AUTOSHIP / Yara Birkeland
模式管理器                            - 物理 + 水动力建模 + 视觉
                                      - HLA / DIS 联邦
                  ◄───────────────  
                  传感器 / 状态反馈

成本：高（License + 集成）
保真度：★★★★★
认证适合度：★★★★★（DNV 认证已有）
```

#### 选项 B · 商业海事 + Unreal（中等成本，最高视觉保真）

```
[算法侧]              [桥接]              [仿真侧]
战术决策层 (C++)       rosbridge / DDS    AILiveSim (Unreal Engine 5)
                                          - 海事专用 V&V
                                          - 真实传感器仿真
                                          - 已用于 Åboat 自主船舶
                                       
                                          或 NVIDIA Omniverse + Isaac Sim
                                          - RTX ray tracing
                                          - PhysX 5

成本：中
保真度：★★★★★（视觉）/ ★★★★（物理）
认证适合度：★★★★（需要补 V&V 证据）
```

#### 选项 C · 开源 Unity + ROS 2（最低成本，最大定制空间）

```
[算法侧]              [桥接]                [仿真侧]
战术决策层 (ROS 2)     ROS-TCP-Connector    MARUS / SMaRCSim (Unity)
                                            - 多船型支持
                                            - 海事传感器
                                            - ROS 2 原生

                  AIS 历史数据 → 场景库（colav_simulator 适配）
                  STL + Gaussian Process → 自动场景生成（Torben 2022 复现）

成本：低（人力为主）
保真度：★★★★（视觉）/ ★★★（物理）
认证适合度：★★★（V&V 证据完全自建）
```

#### 选项 D · MATLAB / Simulink + Unreal（与算法集成最紧）

```
[算法侧]              [集成]                [仿真侧]
战术决策层            Simulink 3D Block     Unreal Engine（嵌入）
（MATLAB/Simulink）                          - Simulation 3D Lidar
                                            - Simulation 3D Radar
                                            - Vehicle Dynamics Blockset

（不在用户技术栈倾向中，作为参考）

成本：中
保真度：★★★★（视觉）/ ★★★★（物理）
认证适合度：★★★★（MathWorks 工具链可追溯）
```

#### 11.5.1 调研给出的工程建议（Spec 决策时参考）

**对于 FCB 项目**：

- **MIL/SIL 早期**：选项 C（Unity + ROS 2 + colav_simulator）—— 快速迭代，低成本
- **SIL 后期 + HIL**：选项 B（AILiveSim Unreal）或选项 A（K-Sim） —— 高保真传感器 + COLREGs 场景库
- **商业认证阶段**：选项 A（K-Sim） —— DNV 已认证，CCS 接受相对容易（K-Sim 是 IACS 内的国际通用工具）
- **场景生成方法学**：跨选项独立。强烈建议采用 **STPA → STL → Gaussian Process** 自动化（Torben 2022 + Pedersen 2022 方法论），并结合 **AIS 历史数据驱动**（Zhu 2022 方法论）做混合场景库

### 11.6 与其他章节的关联

- **§5（CCS 智能系统认可路径）**：本章节 §11.2 SIL 分配方法学 + §11.4 场景模拟器是 CCS 认可"虚拟仿真初试 → 模型测试中试 → 实船验证终试"三段式 V&V 的具体技术实现
- **§6.2 智能系统认可**：本章节 §11.5 验证基础设施栈是 III 类 CBS 认可的 V&V 证据基础
- **§7 决策建议**：本章节为 Spec 阶段引入"V&V 基础设施"决策提供素材（不在 §7 既有决策范围内，而是作为 Spec 阶段新增决策）

### 11.7 关键文献清单（用于 Spec 阶段深入）

| # | 文献 | 核心贡献 | 重要度 |
|---|------|---------|------|
| 1 | Pedersen et al. 2020 *Safety Science* 129:104799 | 仿真验证总体框架 | ★★★★★ |
| 2 | Torben et al. 2022 *Risk and Reliability* 237(2):293-313 | GP + STL 自动化测试 | ★★★★★ |
| 3 | Pedersen et al. 2022 *J. Phys.: Conf. Ser.* 2311(1):012016 | STPA → 自动场景生成 | ★★★★★ |
| 4 | Kufoalor et al. 2020 *Journal of Field Robotics* 37(3):387-403 | 北海 ASV 实船验证 | ★★★★ |
| 5 | Zhu, Zhou, Lu 2022 *MDPI JMSE* 10(11):1588 | AIS 数据驱动 2900 场景 | ★★★★ |
| 6 | Bolbot et al. 2022 | Sobol 序列采样 + 风险过滤 | ★★★★ |
| 7 | MDPI JMSE 13(8):1570 (2025-08) | 综述：自主避碰性能测试 | ★★★★★ |
| 8 | *MODELS 2024 (ACM/IEEE)* | OpenSCENARIO 思路迁移海事 | ★★★★ |
| 9 | Bingham et al. 2019 *MTS/IEEE OCEANS* | VRX (Gazebo 海事仿真) | ★★★ |
| 10 | Vekinis & Perantonis 2023 *arxiv 2307.06688* | Aeolus Ocean (Unity DRL) | ★★★ |
| 11 | Cieślak 2019 *OCEANS Marseille* | Stonefish 开源水下 | ★★ |
| 12 | Park & Kim 2024 *MDPI JMSE* 12(1):4 | STPA + Software FMEA + BN | ★★★★ |
| 13 | Dugan et al. 2023 *TransNav* 17(2):375-381 | STPA 集成测试避碰 DSS | ★★★★ |
| 14 | *arxiv 2603.02487 (2026-03)* | Robust Simulation Framework + Unity Simulink | ★★★★ |
| 15 | DNV-CG-0264 + AROS Class Notation (2025-01) | DNV 自主船 SQ 流程 | ★★★★ |

### 11.8 §11 章节置信度

**H**（所有结论均有公开文献或工业实证支撑，无幻觉项）。

**边界**：
- 验证基础设施栈选型属 Spec § 2 详细设计任务，本章节不做最终决策
- SIL 数值最终值依赖项目级 HAZID workshop（CCS 验船师 + 用户 + 船东三方参与）
- 海事专用场景描述语言尚未标准化，建议短期用自定义 YAML schema

---

## 12. 船长心智模块（Captain Mental Model, CMM）（v1.2 新增）

### 12.1 章节目的与定位

**CMM 是横向设计指导文件，不是运行时模块。** 它把 41 条 COLREGs 规则中与避碰决策直接相关的 14 条核心规则、Endsley 1995 SA 三层模型、Klein 1998 RPD 决策模型、控制反馈环这四套独立证据，组织为一个统一的、可与 L3 战术层各模块映射的"船长操作心智周期"。

**CMM 在本架构中的三种存在形式**：

- **形式 A · 设计指导（Design Reference）**：约束 L3 各模块的设计哲学——任何 ADR 决策都应能反追溯到具体心智阶段
- **形式 B · 可追溯锚点（Traceability Anchor）**：每个 ADR 反追溯到具体心智阶段 + 学术证据（详见 §12.3）
- **形式 C · 认证证据（Certification Evidence）**：作为 CCS 智能系统认可的"决策透明化"证据材料的语义骨架（详见 §6.12）

**关键纪律**：CMM **不**指定具体算法实现 / **不**绑定具体模块 / **保持架构中立**——这是为了让 §13 给出的 3 个候选架构都可以采纳 CMM 而不冲突。CMM 的语义层独立于模块层。

### 12.2 CMM 的四阶段闭环

CMM 把船长经验组织为 **4 阶段闭环：看 → 判 → 动 → 盯 → 重新看**。每阶段对应 Endsley SA 模型的一个或多个层次 + 一个独立的工程职责。

#### Stage 1「看」(感知 / SA Level 1+2)

- **学术基础**：Endsley 1995 SA 三层模型的 **Level 1（Perception，感知元素）** + **Level 2（Comprehension，理解含义）**
- **工程职责**：从原始传感器数据（Radar / AIS / LiDAR / Camera 融合输出）→ 语义化的"威胁列表"（按风险排序的目标 + CPA/TCPA + 类型 + 趋势）
- **核心要素**：
  - 态势聚合（多传感器融合输出 → 统一目标列表）
  - 风险评估（CPA / TCPA / range / aspect 多因子加权评分）
  - 多目标排序（按威胁等级降序）
  - "方位不变距离近"定律的量化（bearing_rate < 0.05°/s + range_rate < 0 → 高碰撞风险）
- **关键参数（初始基线，待 HAZID 冻结）**：
  - **CPA_safe 按水域分级**：开阔水域 1 nm（1852m）/ 沿岸 0.5 nm（926m）/ 狭水道 0.3 nm（556m）/ 港内 0.2 nm（370m）（IMO COLREGs Rule 7 + MarinePublic 工业实操 + STCW 雷达模拟训练实践）
  - 能见度不良修正：能见度 < 2 nm 时 CPA_safe × 1.5
- **学术 + 工业证据**：Endsley 1995 + Sharma 2019 GDTA + Eriksen 2020 + ITU-R M.1371 AIS 标准

#### Stage 2「判」(决策 / RPD + COLREGs 推理)

- **学术基础**：**Klein 1998 RPD（Recognition-Primed Decision）模型** + IMO COLREGs Rule 5-19 形式化
- **工程职责**：从威胁列表 → 避让决策（让路船 / 直航船 / 双方让路 + 行动方向 + 行动时机 + 行动幅度）
- **核心要素**（6 步推理链）：
  1. 会遇分类（Rule 13/14/15：追越 / 对遇 / 交叉 / 安全通过）
  2. 责任分配（Rule 16/17：让路船 / 直航船）
  3. 类型优先序判定（Rule 18：失控 > 受限 > 限吃水 > 渔船 > 帆船 > 机动船）
  4. 时机判定（Rule 16 让路船及早行动 / Rule 17 直航船三阶段）
  5. 行动选择（Rule 8：转向优先 / 减速次之 / 停船兜底）
  6. 效果验证（Rule 8 "明显" + 维持时间）
- **关键参数（初始基线）**：
  - **最小 30° 航向变化**（Rule 8 "大幅"量化，MarinePublic 2026 + Wang 2021 MDPI JMSE）
  - **Rule 17 三阶段时机阈值**（开阔水域，工业实操参考）：T_give_way = 8 min / T_stand_on_warn = 6 min / T_stand_on_act = 4 min / T_emergency = 1 min
  - **追越扇区**：相对方位 112.5° ~ 247.5°（Rule 13 "正横后大于 22.5°"换算）
  - **对遇判定**：相对方位 ±6° 内 + 航向差 170°-190°
- **学术 + 工业证据**：IMO COLREGs 1972（现行版本，E1）+ Wang 2021 MDPI JMSE 9(6):584（直航船 4 阶段）+ Eriksen 2020 Frontiers + Cockcroft & Lameijer 2012 *A Guide to the Collision Avoidance Rules* 7th Ed.

#### Stage 3「动」(执行 / 控制 + 操纵实践)

- **学术基础**：Fossen 2021 *Marine Hydrodynamics* + 控制反馈环 + COLREGs Rule 8 "明显"约束
- **工程职责**：从决策 → 可执行航点序列 + 速度指令（可被 L4 Guidance 直接消费）
- **核心要素**：
  - 转向起始点（前置距离 = max(V × 30s, 500m)）
  - 维持避让航向段（≥ 2 min 满足 Rule 8 "明显"）
  - 平滑回归过渡点（切入角 ≤ 30°）
  - 切入原航线点
  - 安全校验（避让航线不进入浅水 / 禁区 / 其他危险水域，ENC 查询）
  - 效果验证（执行后预期 CPA ≥ CPA_safe × 1.5）
- **关键参数（初始基线）**：
  - **避让维持时间 ≥ 2 min**（Rule 8 "明显"量化）
  - **转弯半径 ≥ 旋回半径 × 1.2**
  - **切入角 ≤ 30°**（超过则插入过渡点）
  - 避让最大速度 ≤ 0.85 × V_max（保留 15% 推力储备）
- **学术 + 工业证据**：IMO COLREGs Rule 8 + Szlapczynski 2015 *JNAOE* + Fossen 2021 + Reza 2025 RL+NMPC + Yasukawa 2016（FCB 高速段，§6.10）

#### Stage 4「盯」(监控 / SA Level 3 + 闭环反馈)

- **学术基础**：Endsley 1995 SA 三层模型的 **Level 3（Projection，预测未来状态）** + 控制反馈环 + COLREGs Rule 17(b) 直航船必须行动条款
- **工程职责**：从执行后的实际效果 → 升级 / 解除 / 紧急切换决策
- **核心要素**：
  - CPA 趋势分析（线性回归斜率：> 1 m/s 改善 / < -0.5 m/s 恶化 / 中间停滞）
  - 升级触发判定（CPA 持续恶化 + TCPA < T_escalate → 加大避让幅度）
  - 紧急切换判定（CPA < 0.5 × CPA_safe + TCPA < T_emergency → 紧急通道 + 跨层指令）
  - 解除条件严格判定（CPA ≥ CPA_safe + TCPA < 0 + range_rate > 0 + threat_score < 20，**全部条件持续满足 ≥ 60s**）
  - 恢复阶段新威胁扫描（避免回归路径上出现新会遇）
- **关键参数（初始基线）**：
  - **CPA 趋势改善阈值**：线性回归斜率 > 1 m/s
  - **升级 TCPA 阈值**（开阔水域）：2 min（沿岸 1.5 min / 狭水道 1 min / 港内 0.75 min）
  - **紧急 TCPA 阈值**（开阔水域）：1 min（沿岸 0.75 min / 狭水道 0.5 min / 港内 0.33 min）
  - **解除确认持续时间**：60s
  - **解除后监控窗口**：120s
- **学术 + 工业证据**：Endsley 1995 + Hareide-Veitch 2024 transparency layer + COLREGs Rule 17(b) + Veitch 2024 *Ocean Engineering*（ROC 接管时间窗实验）

### 12.3 CMM 形式化映射（双向追溯表）

下表是 CMM 的核心工具——**Spec ADR ⟷ CMM Stage ⟷ 上游证据** 的双向追溯。任何决策可正向追溯到学术证据，任何学术证据可反向查询其支撑了哪些 ADR。

| Spec ADR | 主要 CMM Stage | 上游证据 |
|---------|--------------|---------|
| AD-02 多时间尺度混合 MPC + BC-MPC | Stage 2 + Stage 3 | Eriksen 2020 + COLREGs Rule 8/13-17 |
| AD-04 主决策白盒 + ML 边界 | Stage 2 | CCS《船用软件安全及可靠性评估指南》 + Hareide-Veitch 2024 transparency |
| AD-15 COLREGs 9 条规则形式化 | Stage 2 | IMO COLREGs Rule 5-19 + Wang 2021 MDPI JMSE 9(6):584 |
| AD-09 MRC 三模式 | Stage 4 | COLREGs Rule 17(b) + Veitch 2024 *Ocean Engineering* |
| AD-23 SIL 2-3 候选 | 全部 4 Stage | IEC 61508-3 + §11.2.3 Park & Kim 2024 |
| AD-25a STPA + Software FMEA + BN | 全部 4 Stage | §11.2 + §11.2.6 v1.2 新增 |
| **（v1.2 拟新增）AD-04a CMM 横向锚定** | Stage 1-4 自身 | Endsley 1995 + Klein 1998 + 控制论 |
| **（v1.2 拟新增）AD-25f Doer-Checker 双轨** | Stage 2 + Stage 3 + Stage 4 | Jackson 2021 MIT CSAIL + Boeing 777 + RTA 海军航空 |
| **（v1.2 拟新增）AD-19a ASDR 决策审计** | 全部 4 Stage | §6.12 + IACS UR E27 Rev.1 |

> **使用方法**：Spec 阶段每写一条 ADR 都应在表中查询对应的 CMM Stage + 上游证据。如果某条 ADR 无法在表中找到对应行，说明该决策**缺乏证据基础**或**超出本调研范围**——后者应显式标 `[TBD]`，前者必须回炉。

### 12.4 CMM 与 CCS 智能系统认可的对接

CMM 提供 CCS 智能系统认可所需的 3 个关键对接点：

| 对接点 | 内容 | 锚定章节 |
|-------|------|---------|
| **决策透明化** | CMM 4 阶段闭环 + 决策依据链记录（§6.12 形式 1） → 满足 CCS 智能系统认可对"软件可靠性 + 安全证据"的可审计性维度 | §6.2 + §6.12 |
| **白盒可审计** | CMM 不指定算法实现，但要求每个决策能反追溯到具体阶段 + 具体规则条款 + 具体上游证据 → 与 CCS《船用软件安全及可靠性评估指南》白盒证据要求一致 | §6.4 |
| **失效场景的"船员化解释"** | HAZID/STPA 阶段把潜在失效按 4 阶段分类（§11.2.6） → 让 CCS 验船师 + 船东 + 船长在同一语义层评审 | §11.2.6 |

### 12.5 CMM 在 V&V 中的应用

CMM 在 V&V 阶段提供 3 个具体落地路径：

1. **作为 STPA UCA 分析的场景骨架**（§11.2.6）：4 阶段每阶段对应一组潜在 UCA，作为 workshop 初始检查清单
2. **作为场景模拟器的高层语义标签**（§11.4.5）：每个测试场景标注其测试的心智阶段 + 对应 UCAs，形成 UCA × Scenario 双向追溯矩阵
3. **作为行业专家（船长 / 引航员 / CCS 验船师）评审的对接接口**：CMM 的语义对船员可读，避免"算法语言 vs 船员语言"的评审鸿沟

### 12.6 §12 章节置信度

**H**（所有结论均有公开学术或工业证据支撑）。

| 子项 | 置信度 | 证据级别 |
|------|--------|---------|
| 4 阶段闭环本身 | **H** | E4（Endsley 1995 SA + Klein 1998 RPD + 控制论 + Hareide-Veitch 2024）|
| 4 阶段量化参数 | **H** | E1（COLREGs 1972 现行版本）+ E3（MarinePublic 2026 工业实操）+ E4（Wang 2021 MDPI JMSE 等）|
| 4 阶段具体内容 | **H** | E4 主（Endsley + Wang 2021 + Cockcroft 2012）+ E5（SANGO 4 份文档作为对照参考）|
| 与 CCS 智能系统认可对接 | **M-H** | E1（CCS 规范）+ E4（Hareide-Veitch 2024）；最终对接细节由 CCS 验船师确认 |
| 形式化映射表（§12.3）| **M-H** | 表内现有行有充分证据；标"v1.2 拟新增"的 ADR 需要 Spec v1.2 阶段最终确认 |

---

## 13. L3 战术层模块结构调研（架构候选方案）（v1.2 新增）

### 13.1 调研边界

- **范围**：仅 L3 战术层内部模块划分；**不**触及 L1-L2-L4（战略 / 战术 / 控制）层级边界（由 §1 主架构 + Spec AD-01 已冻结）
- **输出**：3 个候选（A / B / C）+ 7 维度对比矩阵 + MPC/BC-MPC 与 Avoidance Planner 的关系澄清
- **立场**：**完全中立**——3 候选都给学术 + 工业证据，本调研**不**给倾向性建议；最终选型留到 Spec §2.1 详细设计阶段做项目级 HAZID + 行业专家（含船长 / CCS 验船师）评审 workshop（对应 §8 TBD-14）
- **不变量**：任一候选都不破坏 §1 主架构 + Spec [平台固定] ADR（AD-01/02/04/05/10/13/15/17/20/21/22/24/25 + AD-25a/b/c + AD-26/27）

### 13.2 候选架构 A · 算法导向 7 模块（Spec v1.1 AD-03 现状）

**模块清单**（Spec v1.1 AD-03 已锁定）：

```
L3 战术层（候选 A · 算法导向 7 模块 + 1 模式管理器）
├── 态势聚合（Situational Awareness）
├── COLREGs 规则引擎（Rule Engine）
├── 风险评估（Risk Assessment）
├── 中程 MPC（Mid-term Model Predictive Control）
├── 近距 BC-MPC（Branching-Course MPC）
├── 局部重规划（Local Replanner）
├── 指令仲裁器（Command Arbiter）
└── 模式管理器（Mode Manager，跨层贯通）
```

**设计哲学**：按算法功能切分模块边界。

**与 CMM 的关系**：CMM 作为外部设计指导文件，在 V&V 阶段对照验证；模块边界 ≠ 心智边界（如 Stage 2「判」涉及 COLREGs 规则引擎 + 风险评估两个模块的协作）。

**优势**：

- 与 Eriksen 2020 多时间尺度混合架构完全对齐
- 工业先例多（Avikus 三段流水线 + Kongsberg 4 模块组的混合体）
- **变更成本最小**——Spec v1.1 已采用此结构

**劣势**：

- 模块边界与船长心智不对齐 → 行业专家评审难度高（需要"算法语言 → 船员语言"翻译层）
- 决策依据链跨模块拼接复杂（每条决策需要追溯到 2-3 个模块的协作）
- ASDR（§6.12 形式 2）实现复杂度较高

**证据基础**：Eriksen 2020 Frontiers + Avikus（§3.3）+ Kongsberg K-MATE 4 模块组（§3.1）+ Spec v1.1 AD-03

### 13.3 候选架构 B · 心智导向 4+1 模块

**模块清单**：

```
L3 战术层（候选 B · 心智导向 4+1 模块）
├── Target Tracker（看 / Stage 1）
│     职责：态势聚合 + 风险评估 + 多目标排序
├── COLREGs Engine（判 / Stage 2）
│     职责：规则推理 + 责任判定 + 行动方向
├── Avoidance Planner（动 / Stage 3）
│     职责：航点序列生成 + 速度规划 + 安全校验 + 效果验证
├── Risk Monitor（盯 / Stage 4）
│     职责：闭环监控 + 升级判定 + 解除判定 + 恢复扫描
└── 模式管理器（Mode Manager，跨层贯通）
```

**设计哲学**：按船长心智阶段切分模块边界。

**与 CMM 的关系**：模块边界 = 心智阶段边界，**天然白盒**；CMM 既是设计指导也是模块定义来源。

**优势**：

- **行业专家评审接口最友好**——船长 / 引航员 / CCS 验船师可直接对应自己的工作流
- 决策依据链与心智阶段直接对齐
- 与 Hareide-Veitch 2024 transparency layer 完全对齐
- ASDR 实现简单（事件按模块即按 Stage 分类）

**劣势**：

- 模块边界与算法不天然对齐——MPC/BC-MPC 都内嵌在 Avoidance Planner 中，不再是独立模块
- Risk Monitor 与 Stage 4 监控职责高度耦合，但**与模式管理器的边界需要进一步澄清**（哪些升级请求由 Risk Monitor 处理，哪些由模式管理器处理？）
- 与 Spec v1.1 AD-03 差距较大，**变更成本中等**——需要重新分配 7 模块的内容到 4 模块

**证据基础**：Endsley 1995 + Hareide-Veitch 2024 + 4 份 SANGO 文档（参考性，§3.5+ 已说明）

### 13.4 候选架构 C · 心智骨架 + 算法插件（混合）

**模块清单**：

```
L3 战术层（候选 C · 2 层嵌套：心智骨架 + 算法插件）
│
├── Stage 1「看」骨架（统一感知接口）
│   ├── 算法插件：CPA/TCPA 计算
│   ├── 算法插件：目标跟踪滤波（Kalman / IMM）
│   ├── 算法插件：运动预测（线性 / 圆弧 / GP / RL）
│   └── 算法插件：威胁评分
│
├── Stage 2「判」骨架（统一决策接口）
│   ├── 算法插件：COLREGs 形式化规则推理
│   ├── 算法插件：Rule 18 类型优先序判定
│   ├── 算法插件：多目标冲突消解搜索
│   └── 算法插件：Doer-Checker 中的 Doer
│
├── Stage 3「动」骨架（统一执行接口）
│   ├── 算法插件：中程 MPC（3-5 min 时域）
│   ├── 算法插件：短期 BC-MPC（30-60 s 时域）
│   ├── 算法插件：航点序列生成
│   ├── 算法插件：安全校验（ENC 查询 / 浅水 / 禁区）
│   └── 算法插件：Doer-Checker 中的 Checker（Route Safety Gate）
│
├── Stage 4「盯」骨架（统一监控接口）
│   ├── 算法插件：CPA 趋势分析（线性回归）
│   ├── 算法插件：升级决策
│   ├── 算法插件：解除判定（严格条件 + 持续确认）
│   └── 算法插件：紧急通道（反射弧式触发）
│
└── 模式管理器（Mode Manager，跨层贯通）
```

**设计哲学**：外层心智骨架（白盒可审计）+ 内层算法插件（可独立优化、独立 V&V、可独立替换）。

**与 CMM 的关系**：CMM 同时是**设计指导**（顶层骨架）和**模块定义**（双重角色）。

**优势**：

- **学术 / 工业 / 合规三方面都最强**：
  - 心智骨架提供白盒可审计性（CCS 智能系统认可 ✓）
  - 算法插件保留 Eriksen 2020 多时间尺度混合的工程实践（AD-02 ✓）
  - 与 Backseat Driver 范式（Benjamin 2010，§3.5）+ MIT Certified Control（Jackson 2021）+ IEC 61508-3 模块化要求完全相容
- 多船型扩展时算法插件可独立替换（如 FCB 的 Yasukawa 2016 高速修正可作为 Stage 3 的 MPC 内部插件，不影响其他船型）
- ML 边界与算法插件层完美重合（AD-04 ML 边界落在算法插件层，骨架层保持纯白盒）
- ASDR 事件 schema 设计自然（按 Stage 分类 + 算法插件作为 sub-field）

**劣势**：

- 抽象层级最复杂（2 层嵌套：骨架接口 + 插件接口）
- **接口契约设计工作量最大**——需要骨架 ↔ 插件接口 + 骨架 ↔ 骨架接口（Stage 1→2→3→4 数据流）
- 工程团队需要在两个抽象层都建立设计能力

**证据基础**：Backseat Driver（Benjamin 2010 *J. Field Robotics*，§3.5 已建立）+ MIT Certified Control（Jackson 2021 arxiv 2104.06178，§4.2 v1.2 新增）+ IEC 61508-3 模块化（§6.11）+ Avikus 三段流水线（§3.3）的演化

### 13.5 三候选对比矩阵

| 维度 | 候选 A 算法导向 | 候选 B 心智导向 | 候选 C 心智 + 算法插件 |
|------|---------------|---------------|---------------------|
| **与 CMM 对齐度** | M（外部对照）| H（模块=心智）| H（骨架=心智，插件=算法）|
| **CCS 智能系统认可可解释性** | M（需翻译层）| H（船员语言直达）| H（骨架可审计 + 插件可深入）|
| **与 AD-02 多时间尺度混合 MPC 契合度** | H（MPC 是独立模块）| M（MPC 内嵌于 Avoidance Planner）| H（MPC 是 Stage 3 算法插件）|
| **与 AD-04 ML 边界契合度** | M（ML 边界跨模块需特别声明）| M（ML 边界 ⊂ 模块内部）| H（ML 边界 = 算法插件层，骨架层纯白盒）|
| **多船型扩展性** | H（动力学是独立插件 AD-26）| M（动力学藏在 Avoidance Planner 内部）| H（动力学是 Stage 3 算法插件，独立替换）|
| **工程化复杂度** | L（扁平 7 模块 + 1 管理器）| L-M（扁平 4+1 模块）| M-H（2 层嵌套 + 双层接口契约）|
| **行业专家评审友好度** | L（算法语言）| H（船员语言）| H（船员语言 + 算法可深入）|

**简短论证**：

- 候选 A 在 AD-02 契合度上最强（MPC/BC-MPC 已是独立模块），但 CCS 可解释性和评审友好度最弱
- 候选 B 在评审友好度和 CMM 对齐度上最强，但 AD-02 契合度和 ML 边界契合度只有 M
- 候选 C 在 5 个维度上 H、1 个维度上 M(ML 边界 H 但成本是 M-H)、工程化复杂度上 M-H——**合规 / 扩展性 / 评审友好度三方面最均衡，但工程化成本最高**

### 13.6 中立结论

**本调研不预选**（按用户决策保持中立）。三候选各有最强维度：

- **候选 A** 是工业算法语言的扁平方案，**变更最小**——适合"先稳定 v1.0 落地、未来再演进"的工程节奏
- **候选 B** 是船员语言的扁平方案，**白盒最直接**——适合"CCS 认证优先、行业专家评审是关键路径"的工程节奏
- **候选 C** 是 2 层嵌套方案，**合规 / 扩展性 / 评审友好度最均衡**——适合"长期演进 + 多船型 + 高合规要求"的工程节奏

**最终选型由 Spec §2.1 详细设计阶段做项目级 HAZID + 行业专家（含船长 / CCS 验船师）评审 workshop 决定**（对应 §8 TBD-14）。任一候选都不破坏 §1 主架构 + AD-01/AD-02 等 [平台固定] ADR，因此不影响 v1.2 冻结。

### 13.7 关于 MPC/BC-MPC 与 Avoidance Planner 的关系澄清

这是 Patch-B 评审中用户特别关注的边界问题，本节明确写入 Research v1.2 防止后续误解：

**MPC / BC-MPC** 是**算法层**（数学优化方法），由 Spec AD-02 选定多时间尺度混合方案（中程 MPC + 短期 BC-MPC + Robust 处理 + 可选 GP/SI 辅助）。

**Avoidance Planner** 是**职责层**（从决策到可执行航点 + 速度指令）。

**二者不是冲突内容，而是不同抽象层的对应关系**：

| 候选 | MPC/BC-MPC 落位 | Avoidance Planner 落位 |
|------|----------------|----------------------|
| **候选 A** | 独立模块（中程 MPC + 近距 BC-MPC 各自一个模块）| 概念被切分到"局部重规划 + 指令仲裁器"两个模块 |
| **候选 B** | Avoidance Planner 模块的内部实现细节（不暴露在模块接口）| 独立的 Stage 3「动」对应模块 |
| **候选 C** | Stage 3「动」骨架的算法插件 | Stage 3「动」骨架的角色等价于 Avoidance Planner 概念 |

**关键纪律**：

- **任何候选都保持 AD-02 选型不变**（中程 MPC + 短期 BC-MPC + Robust 处理 + 可选 GP/SI 辅助），只是**落位**不同
- 这一点已在 §6.10 不确定性建模 + §13.4 候选 C 算法插件清单中显式承接
- **MPC/BC-MPC 选型与模块结构选型是两个正交的决策**——AD-02 已冻结，AD-03 待选

### 13.8 §13 章节置信度

**M-H**：本章节自身定位是"调研材料而非决策"——给候选 + 对比，不做最终选型。

| 子项 | 置信度 | 说明 |
|------|--------|------|
| 三候选的设计哲学和模块清单 | **H** | 每个候选都有学术 + 工业证据基础（候选 A 的 Spec v1.1 / 候选 B 的 SANGO + Endsley / 候选 C 的 MIT Certified Control + Backseat Driver）|
| 三候选的优劣势分析 | **M-H** | 优劣势基于已有证据推导；某些工程化复杂度的具体度量待 Spec §2.1 真正实施时才能精确化 |
| 7 维度对比矩阵的评分 | **M-H** | 每个评分都有简短论证锚定到证据；不排除 Spec §2.1 评审 workshop 时根据项目级具体情况调整 |
| MPC/BC-MPC 与 Avoidance Planner 关系澄清 | **H** | 是抽象层级的逻辑澄清，与 AD-02 选型不冲突 |
| 中立结论 | **H** | 不预选是按用户决策的明确立场 |

**边界**：

- 三候选的最终选型属 Spec §2.1 决策；本章节不做最终决策
- 任一候选的具体接口契约（模块间数据流 schema）待 Spec §3 接口契约 YAML schema 阶段冻结
- 候选 C 的算法插件接口规范属 Spec §2.5 详细设计任务

---

## 10. 变更历史

| 版本 | 日期 | 作者 | 变更说明 |
|------|------|------|---------|
| v0.1 | 2026-04-27 | 用户 + Claude | 初稿，13 项 TBD 待关闭 |
| v0.2 | 2026-04-27 | 用户 + Claude | **TBD 全 13 项闭环**：CCS 单一入级确认（TBD-13）；FCB 走常规船入级（TBD-05）；项目无 CLAUDE.md 确认（TBD-10）；CCS 标志体系（TBD-03）+ 配套指南（TBD-03）+ 智能系统认可（TBD-03）+ AI 政策（TBD-08）+ ROC 接管（TBD-06）+ V2V（TBD-09）+ FCB 高速 MPC（TBD-07）+ Sea Machines TA（TBD-12）+ ABS CONOPS（TBD-04 降级）+ DNV CG-0264 章节（TBD-01/02 降级）；TBD-11 闭源信息保持开放（不阻塞）|
| **v1.0** | **2026-04-27** | **用户 + Claude** | **用户冻结**；进入阶段 B（Spec）；产出 Spec Part 1（BLUF + ADR 表，33 条 ADR） |
| **v1.1** | **2026-04-27** | **用户 + Claude** | **合并 Patch-A**：(1) 修订 N+ 写法（CCS 标志体系实际无 N+，§6.1 §6.9 多处修订）；(2) 新增 §11 SIL 验证路径与场景模拟器章节（含 SIL 分配方法学、IMO MASS Code 五段式测试流程、场景构造方法学三类、Unreal/Unity 海事仿真生态、4 种验证基础设施栈选项）；(3) 关闭 Spec Part 1 识别的 6 项 [TBD]（LOW-01 ~ LOW-06）；(4) FCB 入级建议改为 i-Ship (Nx, R1, M, E, I) 进阶 / i-Ship (N, R1, M, E, I) 基础双方案 |
| **v1.2** | **2026-04-28** | **用户 + Claude** | **合并 Patch-B**（SANGO 4 份文档作为当前设计参考的结构性吸收 + 船长心智模块 CMM 抽象 + L3 模块结构调研）：(1) §3.1-§3.5 五候选新增"心智白盒映射度"评估行；(2) §3.5 后新增"当前设计参考 · SANGO MASS_ADAS L3"小段（不入 §3.6 对比表）；(3) §4.1 新增 4 篇核心文献（Endsley 1995 *Human Factors* / Hareide-Veitch 2024 JMST / Sharma 2019 *Safety Science* / Wang 2021 MDPI JMSE 9(6):584）；(4) §4.2 新增 1 篇较新文献（Jackson 2021 MIT CSAIL Certified Control · arxiv 2104.06178）；(5) §4.3 算法对比表新增 Doer-Checker 行（标注架构层非算法）；(6) §6.1 末尾追加决策可解释性维度段；(7) **新增 §6.12 决策透明化作为 CCS 智能系统认可的扩展证据**（5 子节，含 ASDR + Doer-Checker + 决策依据链记录三件套）；(8) §6.4 工程结论后追加 Doer-Checker 落地段；(9) **新增 §11.2.6 船长心智作为 STPA UCA 分析的场景骨架**（4 阶段 × 潜在 UCA 类型映射表）；(10) §11.4.5 追加 CMM 阶段语义标签段（场景模拟器 YAML schema 字段建议）；(11) **新增 §12 船长心智模块 CMM**（精炼版，6 子节：定位 / 4 阶段闭环 / 形式化映射 / CCS 对接 / V&V 应用 / 置信度）；(12) **新增 §13 L3 战术层模块结构调研**（中立调研：3 候选 A/B/C + 7 维度对比矩阵 + MPC/BC-MPC 与 Avoidance Planner 关系澄清，**保持中立不预选**，最终选型留 Spec §2.1）；(13) §8 新增 TBD-14（L3 模块结构最终选型）+ TBD-15（CMM 与各模块绑定方式）；(14) §9 决策建议新增 10-13（CMM / Doer-Checker / Rule 17 三阶段量化 / ASDR）+ 项目级变量新增 1 项；(15) 附录 A 修订图 2 描述以反映 §13 三候选并存。**主架构决策（§1-§5）+ Spec [平台固定] ADR 不变**；SANGO 4 份文档不作为单独章节、单独候选；最终模块结构选型留 Spec §2.1 HAZID + 评审 workshop。整体增量约 7000 字，整体置信度仍为 H |

---

## 附录 A · 候选架构与三张可视化图

本调研附带 3 张架构可视化图（Claude 在对话中渲染）：

- **图 1 · 战术决策层系统上下文**：三层架构（战略 / 战术 / 控制）+ 感知融合输入 + ROC 双向接口 + 船舶执行机构
- **图 2 · 战术决策层内部模块**：v1.1 时为 7 模块 + 模式管理器（候选 A 现状）；**v1.2 起 §13 给出 3 候选**（A 算法导向 7 模块 / B 心智导向 4+1 模块 / C 心智骨架 + 算法插件混合），最终选型由 Spec §2.1 决策
- **图 3 · 模式管理状态机**：D2 ↔ D3 ↔ D4 + MRC 兜底 + ROC override

图 2、图 3 是 §9 决策建议的视觉对应。v1.2 中图 2 的 3 候选可视化对照可在 Spec §2.1 阶段做 HAZID workshop 时补充。

---

*本 Research v1.2 已冻结，是后续 Spec 阶段的唯一真源。Spec 中的任何决策不得超出本调研的证据范围；如需扩展，要么先更新本调研到 v1.3，要么在 Spec 中显式标记为 [TBD]。*

*v1.2 在 v1.1 基础上合并了 Patch-B（SANGO 4 份文档作为当前设计参考的结构性吸收 + 船长心智模块 CMM 抽象 + L3 模块结构调研，详见 §10 变更历史 v1.2 行）。主要架构决策（§1-§9 主体）保持不变；新增 §6.12 决策透明化 + §11.2.6 CMM 作为 STPA UCA 骨架 + §12 船长心智模块 CMM + §13 L3 模块结构调研，作为后续 Spec v1.2 详细设计的输入。所有新增内容均带 E1/E4 上游证据；SANGO 4 份文档（E5）仅作为对照参考，不作为决策唯一依据。*
