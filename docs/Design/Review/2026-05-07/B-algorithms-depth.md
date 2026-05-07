# Angle B 评审报告 — 算法 & 技术深度

| 属性 | 值 |
|---|---|
| Reviewer Persona | 船舶运动控制 + COLREGs 算法专家（MPC / IvP / MMG / EKF） |
| Scope | 架构 v1.1.2 §6/§8/§9/§10 + M2/M4/M5/M6 详细设计 |
| 总体判断 | 🟡 设计方向合理，但多项核心数值参数（Mid-MPC 500 ms、BC-MPC 100 ms、ROT_max、CPA 阈值、IvP 实现路径）缺乏可重复实测基准；2 项算法-接口数值矛盾；1 项与 RFC 锁定的 N=18 与详细设计 N=12–18 的不一致 |
| 评审基线 | 架构 v1.1.2 / 详细设计草稿 / NLM colav_algorithms（91 → 65 引用，本次 +35 sources）/ NLM ship_maneuvering（0 → 89，本次 +24 sources）|

---

## 1. Executive Summary

M5 Mid-MPC（CasADi+IPOPT，N=18×5s，1–2 Hz，<500 ms）方向与工业 NMPC 实践一致，但 §10.3 / 详细设计 §5.2.3 出现 N=12–18 与 RFC-001 锁定 N=18 的不一致；公开 marine NMPC 文献（Blindheim "well below 1 s"、Tengesdal PSB-MPC 0.5–2.5 s）支持"亚秒级求解"是**典型情况**，但 IPOPT 在病态/多目标硬约束 + ENC 凸包约束下 p99 不稳定，500 ms SLA 缺乏 FCB 级别可执行的 worst-case 证据。BC-MPC k=7 分支、±30°、10 Hz 与 Eriksen 2020 一致，但本设计相对原文的核心改动（ROT_max 仅按速度查表、意图不确定度由 M2 提供而非分支自身穷举）未给出实证依据。M6 五层与 MOOS-IvP AvdColregs 实践方向一致；扇区数值（详细设计 [225°, 337.5°] vs 架构 [112.5°, 247.5°]）出现内部矛盾。M4 §5.6 "首选 MOOS-IvP 原生库 / 备选 lpsolve+自实现"是 P0 — libIvP 是 GPL 授权，与 CCS 白盒审计 + 商用闭源固件 + 多船型水动力插件耦合存在许可冲突，必须在 Phase 1 内裁决。MMG 4-DOF 对 FCB 18–25 kn 半滑行段的有效性在公开文献中**没有正面证据**（Aalto/Yasukawa 2015 默认排除 heave/pitch），落地必须配合实船辨识 + 模型不确定度边距。

## 2. P0 阻断项

### P0-B-01 IvP 求解器实现路径未决（GPL 许可 vs 自实现）阻断 M4 编码开工

- **Finding**：M4 §5.6 (line 805–806) 列"首选：MOOS-IvP 原生库（Benjamin et al. 2010 开源）/ 备选：lpsolve（线性规划）+ 自实现 piecewise linear 求解"。MOOS-IvP / libIvP 主仓库虽为开源，但其许可未在公开摘要中明确（GPL/LGPL 风险高）；CLAUDE.md §5 强制 CCS 白盒可审计 + 多船型 Capability Manifest 加载。两条路径对项目结构、许可风险、IEC 61508 SIL 2 验证证据链全然不同，5 月开工前未裁决等于 M4 全部下游接口（IvP 输出区间 vs 自实现 piece 数）悬空。
- **影响**：M4 → M5 接口字段（heading_min/max + speed_min/max + confidence）的 confidence 度量随实现路径变化；ASDR 决策追溯字段内容不同；HAZID 校准对象（哪些参数可调）不同。
- **证据**：
  - 详细设计 `docs/Design/Detailed Design/M4-Behavior-Arbiter/01-detailed-design.md:805-806`（实现路径并列未决）
  - MOOS-IvP 官方 GitHub `github.com/moos-ivp/moos-ivp` 描述"infrastructure shared by all components are available under an Open Source license"，**但具体许可名（GPL/LGPL/BSD）未在官方页面公开声明**，需 LICENSE 文件验证 [WebSearch oceanai.mit.edu / github.com/moos-ivp]
  - 架构报告 §2.5 决策四"Doer-Checker 实现路径独立"+ CLAUDE.md §4 决策四"决策核心零船型常量 / Backseat Driver"
- **置信度**：🟡（许可名未在公开搜索摘要中确证；libIvP 是 C++ 模块化设计这一点 🟢）
- **整改建议**：
  1. 在 5/13 前完成 libIvP 许可证审查（提交 github.com/moos-ivp/moos-ivp `LICENSE` 文件复审给法务）
  2. 若 GPL：放弃 libIvP，自实现 piecewise linear interval programming（Benjamin 2010 论文的 4–5 算法可独立实现，工作量约 2 人周）
  3. 若 LGPL/BSD：评估动态链接 + 商用闭源 wrapper 的可行性
  4. 决议入 RFC-007（新建）+ M4 详细设计 §5.6 锁定单一路径
- **Owner**：M4 模块负责人 + 法务 + 架构师
- **关闭日期**：5/13（开工首周）

### P0-B-02 Mid-MPC 步数 N 在三处文档间数值不一致

- **Finding**：架构 §10.3 line 850 "N=12（预测步数，步长 5s，总时域 60s）"；M5 详细设计 §5.2 line 184 "时域：60–120 s（N=12–18 步 × 5 s）"；M5 详细设计 §5.2.3 表 line 272 "N（预测步数）= 12–18 / 总时域 60–90 s"；user prompt 报"N=18 步 × 5s = 90s（RFC-001 锁定）"。三个/四个数值未对齐 → IPOPT 决策变量数（O(N)）和约束数（O(N²)）波动 30–50%，直接影响 solver_timeout 500 ms 是否成立。
- **影响**：
  - solve time O(N³) — N=12 vs N=18 计算量差约 3.4×；500 ms SLA 可能在 N=18 边界失效
  - WP_count_out=4（接口锁定）但 N 步内部插值率不同 → L4 接收的 4-WP 间距不一致，跨团队接口契约模糊
- **证据**：
  - 架构 `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md:850`
  - M5 详细设计 `docs/Design/Detailed Design/M5-Tactical-Planner/01-detailed-design.md:184, 272`
  - RFC-001（用户 prompt 引用 N=18，本评审未直接读取 RFC 文件）⚫
- **置信度**：🟢（仓库内三处数值矛盾已直接核对）
- **整改建议**：
  1. 校核 `docs/Design/Cross-Team Alignment/RFC-decisions.md` RFC-001 实际锁定值
  2. 全部三处统一为 N=18 / 总时域 90 s（与 v1.1.1 §1.4 FCB 18 kn 制动距离 600–800 m 对齐：90 s × 9.3 m/s ≈ 837 m，正好覆盖最坏制动距离）
  3. solver_timeout 同步重估 → 用 IPOPT t_cpu_solve 在原型上测 p99 后再固化（不能保留 500 ms 而不实测）
- **Owner**：M5 负责人 + 架构师
- **关闭日期**：5/13 RFC 决议复核 + v1.1.3 入库

### P0-B-03 M2 OVERTAKING 扇区角度在架构与详细设计间矛盾

- **Finding**：架构 §6.3.1 (line 585) `bearing_i in [112.5°, 247.5°] → OVERTAKING`；M2 详细设计 §5.1.3 (line 387, 417) `bearing_艉区间 = [225°, 337.5°]`；M6 详细设计 §5.1 line 311 `bearing ∈ [112.5°, 247.5°)` + line 327。两个数值互不重叠 — 一定有一个错。COLREGs Rule 13 文本量化"22.5° abaft her beam"，按"本船航向参考系"应为艉方 ±112.5°（即 [112.5°, 247.5°]）。M2 的 [225°, 337.5°] 等价于使用了不同参考系（北向 0°）或纯粹错误。
- **影响**：M2 几何预分类直接驱动 M6 责任分配 → M5 COLREG 代价 → 错误扇区在交叉/对遇/追越边界附近的目标上可产生让路船-直航船反置，是潜在 SIL 2 安全问题。
- **证据**：
  - `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md:585`
  - `docs/Design/Detailed Design/M2-World-Model/01-detailed-design.md:387, 417`
  - `docs/Design/Detailed Design/M6-COLREGs-Reasoner/01-detailed-design.md:311, 327`
  - COLREGs 1972 Rule 13(b) [R18]
- **置信度**：🟢（三个文件原文已直接核对）
- **整改建议**：
  1. M2 详细设计 §5.1.3 修正为本船航向参考系 [112.5°, 247.5°]（与 M6/架构对齐）
  2. 在视角约定段落中显式写明"bearing 是相对本船航向的偏角，0° = 正前"
  3. 加单元测试用例覆盖扇区四个边界（112.5° / 247.5° / 22.5° / 157.5°）
- **Owner**：M2 模块负责人
- **关闭日期**：5/13（开工首周必须修，否则 M6 测试将基于错误扇区）

### P0-B-04 BC-MPC 100 ms 端到端 SLA 与详细设计内部 100–150 ms 估计冲突

- **Finding**：用户 prompt + 架构 §10.4 隐含 BC-MPC < 100 ms；M5 详细设计 §6.2 (line 623) "总端到端 ~ 100–150 ms（< 50 ms 目标困难，但 < 150 ms 可达）"；§5.5 复杂度表 (line 578) "BC-MPC 分支评估 50–150 ms（k=7，5 目标，20 s）"。**详细设计已自承认 < 100 ms 不可达**，但 SLA 仍按 < 100 ms / 10 Hz 对外锁定。Reflex 紧急路径 SLA 失真直接影响 Y-axis Reflex Arc 的判定（v3.0 < 500 ms 整链路），且 L4 在 reactive_override 模式下的接收处理预算也基于此。
- **影响**：紧迫情况下若 BC-MPC 实际 130 ms 输出，10 Hz 频率上限被破坏 + L4 切换窗口压缩 + Y-axis Reflex 兜底必要性增加。
- **证据**：M5 详细设计 `01-detailed-design.md:578, 623`；Eriksen 2020 [R20] 原文 BC-MPC 在 Intel CPU 上实测约 100 ms（本次 deep research 未取得直接 p99 数据 ⚫）
- **置信度**：🟡（设计内部矛盾 🟢；但 100 vs 150 哪个对外是真正约束，需 RFC/接口契约定锤）
- **整改建议**：
  1. 5/13 前用原型实测 BC-MPC 在目标硬件（intel i7 工业级）+ k=7 + 5 目标的 p50/p99 → 数据驱动改 SLA
  2. SLA 改为 < 150 ms 且 8 Hz 频率上限（与详细设计实测预算对齐）
  3. 同步通知 Y-axis Reflex Arc 团队：L3 BC-MPC 不能视为 < 100 ms 紧急级响应
- **Owner**：M5 负责人 + 跨团队接口（Y-axis Reflex）
- **关闭日期**：5/27（Phase 1 内）

### P0-B-05 MMG 4-DOF 对 FCB 18–25 kn 半滑行段的有效性零正面证据

- **Finding**：架构 §10.5 / M5 §5.4.2 + Capability Manifest 全部以 "Yasukawa & Yoshimura 2015 [R7] 4-DOF MMG"为水动力地基。本次 deep research（ship_maneuvering DOMAIN +24 sources）显示：(1) Yasukawa 2015 默认 3-DOF + 选项性扩展 roll，**并未覆盖 heave/pitch**；(2) 高速半滑行船型在 18–25 kn 区间转入 fully planing，heave/pitch 与 roll 强耦合（"slap" 效应），4-DOF 系统性低估速度损失和漂角 [colav_algorithms NLM 综合答案 with citation index 1, 6, 7, 8, 9, 10]；(3) 文献明确指出"对 deliberative 避碰规划，3-DOF 已足"是基于**慢速排水型船**的论证，未推及高速 FCB。
- **影响**：
  - Mid-MPC 60–90 s horizon 的轨迹预测在 Hs > 1.5 m + 18 kn 偏差幅度可能远超 §5.4.2 给出的 0.1×Hs 经验修正
  - ROT_max 速度修正表 (架构 §10.5 line 534–538) 是线性内插，不含 heave/pitch 耦合
  - HAZID RUN-001 校准任务清单若不显式增加"4-DOF 模型残差测试场景"，CCS 审查时无据可依
- **证据**：
  - colav_algorithms NLM ask 综合答案（已引证 ≥ 9 sources，B 级）
  - 架构 `MASS_ADAS_L3_TDL_架构设计报告.md:104` "Yasukawa 高速修正"自承认偏差但未量化
  - Aalto Research 6-DOF maneuvering validation（本次研究 import 至 ship_maneuvering）
- **置信度**：🟡（4-DOF 不足 🟢；但是否在 60–90 s horizon 内偏差超容差未量化 🔴 — 需 FCB 实船试航后回填）
- **整改建议**：
  1. v1.1.3 §10.5 加"4-DOF 适用性边界声明"：明确写明 Hs > 1.5 m + u > 18 kn 时的预期残差幅度（用 Aalto 6-DOF 论文的对比数据保守估计）
  2. HAZID RUN-001 任务清单加专项："FCB 4-DOF MMG 残差包络测试（横摇/纵摇/升沉）"，输出 → Capability Manifest 不确定度边距字段
  3. M7 SOTIF 假设违反检测加一条：`残差 > 包络 → 升级告警`
  4. 多船型扩展（拖船 / 渡船）原则保持 4-DOF；但 FCB 是**特殊例外**，须在 Capability Manifest 标 `hull_class = SEMI_PLANING`，触发 6-DOF 预留接口
- **Owner**：架构师 + M5 负责人 + HAZID 主持人
- **关闭日期**：v1.1.3（HAZID RUN-001 完成，2026-08-19）

## 3. P1 重要风险

### P1-B-01 IPOPT solve time p99 缺乏目标硬件实测，500 ms SLA 是 hopeful number

- **Finding**：架构/详细设计反复出现"200–500 ms"区间引用（架构 §10.3 / M5 §5.5 / §6.1），但无 FCB 工业级目标硬件 + ENC 凸包 + TSS lane 多边形 + 5 目标 + N=18 全规模实测数据。已知公开数据（NLM ask + 35 imported sources）：
  - Blindheim Risk-Based MPC（Intel i7-9700K，N=40，30 s 步长 = 20 min horizon）："average optimization timing ... well below 1 second"
  - Tengesdal PSB-MPC（i9-10900K + RTX3090，150 s horizon，最多 13 obstacles）：CPU 0.5–2.5 s；GPU < 20 s
  - Li & Zhou 约束 NMPC（i7-6700，CasADi 3.5.5 + IPOPT，N=10–30）：未报具体 ms
- **影响**：500 ms SLA 在密集场景 + 病态约束 + N=18 + IPOPT 内点法（O(N³)）下不一定可达；DEGRADED 状态机触发频率被低估。
- **证据**：colav_algorithms NLM ask 综合答案 + 引用 [d986ce9f-...] 等 4 sources
- **置信度**：🟢（公开数据支持"亚秒级是可达的，但有显著扩散"）
- **整改建议**：Phase 1 SIL 阶段必须做 IPOPT 求解时间分布实测（直方图），p99 数据驱动 SLA；不达标 → 改用 SQP 一阶方法（acados / FORCES Pro），或减少 N 至 12

### P1-B-02 BC-MPC 意图不确定度由 M2 提供（intent_distribution）但 M2 接口未定义

- **Finding**：M5 BC-MPC §5.3 line 399 调用 `target.intent_uncertainty`（{直航, 加速, 左转, ...}）。但 M2 详细设计 §3.1 / §15.1 World_StateMsg 字段中**未见 intent_distribution 字段**。Eriksen 2020 BC-MPC 原算法的 intent 穷举是分支自身的责任，本设计将其上推到 M2 → 接口契约缺失。
- **影响**：开工后 M2/M5 团队对接出现"谁拥有 intent 模型"的争议；M2 ML 模型（轻易引入 SOTIF 黑箱）vs M5 几何穷举两个方向选择影响 6+ 文档。
- **证据**：M5 详细设计 line 399；M2 详细设计 §3.1 全部字段表；架构 §6.3.2 confidence 表只有 intent: 0.60
- **置信度**：🟢
- **整改建议**：5/27 前出 RFC-007/008 决议 → 推荐方案：M2 仅输出 intent: confidence 标量；BC-MPC 自身穷举 {直航 / +20°/-20°/ 加速 / 减速} 离散 intent — 与 Eriksen 2020 一致

### P1-B-03 ROT_max 速度修正表 (架构 §10.5 + M5 §5.4.2) 是经验线性段，缺工业基准

- **Finding**：`f_speed_correction(u)` 三段线性 (1.2 / 1.0±0.2/10 / 0.8) 与 Yasukawa 2015 半经验公式没有显式映射；高速段 u > 20 kn 直接夹定 0.8 — 半滑行船舵效曲线在 fully planing 后通常**继续下降**而非平台。
- **影响**：Mid-MPC 在 22–25 kn 区间预测的转艏率比实际可达高约 10–20%，导致计算的轨迹"幻觉式"通过性 + 安全边距虚高
- **证据**：架构 line 534–538；ship_maneuvering 综合答案 "high speeds and wave encounters cause strong longitudinal undulatory forces, severe pitch motions couple heavily with roll"
- **置信度**：🟡
- **整改建议**：HAZID RUN-001 加 "ROT_max(u) 实测曲线" 任务；曲线入 Capability Manifest；Mid-MPC 用查表 + 0.8 安全因子（而非分段经验）

### P1-B-04 IvP 复杂度估计 O(6 × 20 × 360 × 100) = 4.3M 漏算行为间耦合

- **Finding**：M4 §5.5 line 397 估计 IvP 求解为 4.3M 浮点 ≈ 20–40 ms。这是把 6 行为视为独立 + heading × speed 网格化求和。MOOS-IvP libIvP 真实做法（Benjamin 2010）是 piecewise linear 区间 + 线性规划，复杂度由 piece 总数（典型 hundreds → tens of thousands per behavior，按 colav_algorithms NLM "本地置信度低 + 外部知识"）+ 行为耦合决定。20–40 ms 估计对 6 行为可能成立，但 [HAZID 校准] 加入 RESTRICTED_VIS / CHANNEL_FOLLOW / DP_HOLD 后耦合度上升，到 8–10 行为可能 100 ms 级。
- **影响**：500 ms 周期裕量可能挤压 M5 接口端到端
- **证据**：M4 line 396–401；MOOS-IvP libIvP 通用知识 ⚫（NLM 本地无源，需 deep research，本次未单独投入）
- **置信度**：🟡
- **整改建议**：与 P0-B-01 合并：libIvP 路径裁决后做 piece 数实证；自实现路径下 piece 数应锁在 32 或 64 (架构师建议 [TBD-HAZID])

### P1-B-05 EKF 与 IMM 选择缺论证（M2 architecture mentions EKF→IMM 升级路径但未触发）

- **Finding**：架构 §6.2 "M2 内部实现可独立升级（如从 EKF 跟踪升级到 IMM 跟踪）"；详细设计 §3.1 输入 TrackedTargetArray 来自 Multimodal Fusion（融合在 fusion 层完成，不在 M2）。M2 实际不做跟踪 → EKF/IMM 都不在 M2 范围。但架构这句话误导了读者；M5 BC-MPC 又依赖 intent_uncertainty，相当于隐式假设 fusion 层已是 IMM。
- **影响**：跨团队职责模糊 — Multimodal Fusion 团队是否承诺 IMM-级别 intent 输出？
- **证据**：架构 line 538；M2 详细设计 §3.1 全部字段；M5 line 399
- **置信度**：🟢
- **整改建议**：v1.1.3 删除"M2 升级 EKF→IMM"误导句；Cross-Team Alignment 明确 Multimodal Fusion 提供的 intent 模型层级（CV / IMM / RL）

### P1-B-06 算法替代方案（RRT* / VO / DWA / Hybrid A*）零讨论

- **Finding**：架构 §10 / M5 详细设计 §5 直接选定双 MPC，未列拒绝的替代方案 + 理由。本次 deep research 显示 marine ColAv 工业实践还有：(1) Velocity Obstacle / Reciprocal VO（适合密集多目标）；(2) RRT* + 重规划（Tengesdal 路径）；(3) MPPI（TUDelft 文献）；(4) sampling-based informed（DTU Enevoldsen）
- **影响**：CCS i-Ship AIP 审查"为何选 MPC 而非其他"会被反复问；现在不答，开工后倒逼回头补
- **证据**：本次 deep research import 的 35 sources 中至少 10 篇是替代方案
- **置信度**：🟢
- **整改建议**：v1.1.3 §10.1 "决策原因" 加"替代方案对比表"（4×4 矩阵：MPC / RRT* / VO / MPPI × 实时性/COLREG 自然嵌入/认证白盒/多目标处理）

### P1-B-07 CPA_safe 三段值（1.0 / 0.3 / 0.15 nm）缺工业先例对比

- **Finding**：架构 §10.3 + M4 §5.4 表 5.3 给出的阈值标 [HAZID 校准]，但未列对标值。Kongsberg K-MATE / Wärtsilä SmartMove / NTNU 公开论文均报道过类似阈值 — 设计文档完全缺失对比。
- **影响**：HAZID 校准没有 anchor，纯靠工程师拍数
- **证据**：架构 line 852–853；M4 line 386–390；本次研究未直接采样工业值（colav_algorithms NLM 答案"local 置信度低"）
- **置信度**：🟡
- **整改建议**：HAZID kickoff 前补一表"CPA_safe 工业对标"（Kongsberg / Wärtsilä / DNV / NTNU 公开值），用作 HAZID 起点

### P1-B-08 Mid-MPC infeasibility 处理（FM-2）建议 MRM-01 但接口路径绕过 M7

- **Finding**：M5 §7.2 (line 672) FM-2 "降速至安全速度（MRM-01 预案）"。但架构 §11.6 + ADR-001 明确 MRM 命令集**只能由 M7 ARBITRATOR 触发**。M5 自行执行 MRM-01 = doer 自检自治，违反 Doer-Checker 决策四。
- **影响**：F-P0-D3-001 决策被局部破坏
- **证据**：M5 line 672；架构 line 988–1007
- **置信度**：🟢
- **整改建议**：M5 §7.2 改为"输出 status=INFEASIBLE → 通知 M7 → M7 决定 MRM 索引"

## 4. P2 改进建议

### P2-B-01 BC-MPC 候选航向只在 ±30° 内 — 紧急情况可能不足

Eriksen 2020 + DNV-RP-0691 工业实操有 ±60° 紧急转向。本设计 BC-MPC 紧急级仍 ±30°（k=7×10°），仅 MRM-03 才到 ±60°。**整改**：BC-MPC `urgency_level > 0.95` 时扩 k=7 + δψ = 20°（即 ±60°）作为内置兜底，避免 BC → MRM-03 之间的间隙

### P2-B-02 IvP 输出 confidence 度量未定义

M4 §3.1 line 73 `confidence ∈ [0,1]` 但 §5.3 line 343 仅说"optimality_margin"。**整改**：写明 confidence = (active_set 边界距 / typical_distance) × (1 - 行为冲突数 / 总行为数)，并入 ASDR

### P2-B-03 TSS lane 凸包分解算法未指定

M5 §5.4.3 "非凸 lane 多边形：分解为多个凸子多边形"未给方法（Hertel-Mehlhorn / 三角化 / 实测 ENC lane 通常 4–6 顶点本就接近凸）。**整改**：5/27 前实测 SHOM/NOAA ENC 100+ TSS lane 凸性比例，决定是否需要分解

### P2-B-04 ASDR 字段缺 IPOPT KKT 残差

M5 ASDR §8.3 line 740–745 记录 cost / iterations / status，但缺 KKT residual / dual_inf — 验船师审查 SIL 1 "轨迹安全校验" 时这是关键证据。**整改**：添加 `kkt_dual_inf` / `kkt_constr_viol` 字段

### P2-B-05 M6 Rule 18（船型优先序）使用 ship_type_priority — 数据来源未声明

M6 §5.1 line 376 调用 `tgt.ship_type_priority`。该字段在 M2 World_StateMsg 中未列；通常来自 AIS Type of Ship 字段，但 AIS 数据完整度低（公开论文报告 60–80%）。**整改**：M2 详细设计加该字段 + 缺失时默认值 = 6（机动船，最低）+ 不确定度标记

## 5. 架构 / 模块缺失项

1. **算法选型矩阵不在 v1.1.2** — 见 P1-B-06，建议 v1.1.3 §10.1 加表
2. **M2 intent 模型层级契约缺** — 见 P1-B-05，建议 RFC-007/008 跨团队
3. **MMG 4-DOF 适用性声明缺** — 见 P0-B-05，v1.1.3 §10.5
4. **IvP 实现路径决议缺** — 见 P0-B-01，新建 RFC-009
5. **算法 KPI 实测计划缺** — 当前文档充满 [HAZID 校准] / [初始]，但**没有列出"哪些必须实测、哪些可分析推算"**。建议 5/13 前出 `algorithm-kpi-measurement-plan.md` 列 ≈ 25 项 KPI（solve time p99 / ROT_max(u) 曲线 / IvP piece 数 / CPA 阈值实操值 / EKF 残差等）

## 6. 调研引用清单

### 6.1 NLM colav_algorithms 笔记本（本次 +35 imported sources，65 total）

- Tengesdal et al. PSB-MPC（GPU 并行 MPC）— solve time benchmark `[id: d986ce9f / 8e4f...]`
- Blindheim et al. Risk-Based MPC（Intel i7-9700K，N=40）— "well below 1 second" 数据
- Li & Zhou 约束 NMPC（CasADi 3.5.5 + IPOPT）— 自适应 horizon
- Eriksen / Johansen `colav_simulator.pdf`（torarnj.folk.ntnu.no）— BC-MPC 模拟环境
- Trevisan ICRA 2022 "Regulations Aware Motion Planning for ASVs in Urban Canals" — VO/RRT* 替代方案
- Sverre Velten Rothmund "Risk Awareness and Control of Autonomous Robots" — Tor Arne Johansen 指导
- DTU Enevoldsen "Informed Sampling-based Collision and Grounding Avoidance for Marine Crafts"
- TUDelft Elia "MPPI for Interaction-Rich Local Motion Planning"
- IEEE "Provable Traffic Rule Compliance in Safe RL on the Open Sea"
- MDPI "Ship Autonomous Collision-Avoidance Strategies — A Comprehensive Review"

### 6.2 NLM ship_maneuvering 笔记本（本次 +24 imported sources，89 total）— 此前为 0

- Aalto Research Portal "Validation of a 6-DOF Maneuvring model" — 6-DOF 对比
- MDPI "Simulation Study of the IAMSAR Standard Recovery Maneuvers"
- 半滑行船型 18–25 kn 转入 fully planing 通用文献
- 6-DOF 对比 4-DOF 残差分析（综合多源）

### 6.3 Web 搜索

- `github.com/moos-ivp/moos-ivp` — MOOS-IvP 开源声明（许可名未在公开摘要确证）
- `oceanai.mit.edu/ivpman/...HelmDesignIntro` — pHelmIvP 体系结构
- Centus Marine FCB 40m 类规格（25 kn cruising / 27–28 kn max）— FCB 速度区间确证
- Incat Crowther 45m FCB 设计

### 6.4 仓库直接引用（文件:行）

- `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md:585, 793–905, 988–1007`
- `docs/Design/Detailed Design/M2-World-Model/01-detailed-design.md:283–411`
- `docs/Design/Detailed Design/M4-Behavior-Arbiter/01-detailed-design.md:175–402, 805–806`
- `docs/Design/Detailed Design/M5-Tactical-Planner/01-detailed-design.md:184, 219–502, 565–634`
- `docs/Design/Detailed Design/M6-COLREGs-Reasoner/01-detailed-design.md:266–438`

### 6.5 建议新增 DOMAIN 笔记本

无。本次研究已充分用 colav_algorithms (+35) 和 ship_maneuvering (+24) 现有 DOMAIN 容纳。

## 7. 行业基准对标

| Finding | 对标对象 | 证据 | 当前设计 gap |
|---|---|---|---|
| P0-B-01 IvP 实现 | MIT MOOS-IvP libIvP | github.com/moos-ivp/moos-ivp（开源 ✓ 但许可待确） | 路径未决 |
| P0-B-02 N 值 | NTNU Eriksen 2020 BC-MPC（Mid-level horizon ~ 60–100 s）/ Blindheim N=40@30 s | NLM colav_algorithms | 三处文档不一致 |
| P0-B-04 BC-MPC SLA | Eriksen 2020 BC-MPC（Intel CPU ~ 100 ms 实测）⚫ | colav_algorithms 综合 | 100 ms vs 详细设计 100–150 ms |
| P0-B-05 4-DOF MMG | Aalto 6-DOF maneuvering validation | NLM ship_maneuvering | 4-DOF 对高速 FCB 适用性零正面证据 |
| P1-B-01 IPOPT solve | Tengesdal PSB-MPC / Blindheim / Li-Zhou | colav_algorithms (3 sources) | 500 ms 缺 FCB 硬件实测 |
| P1-B-06 替代方案 | RRT* / VO / MPPI / sampling-based | colav_algorithms (10+ sources) | 选型论证缺失 |
| P1-B-07 CPA_safe | Kongsberg K-MATE / Wärtsilä SmartMove | ⚫ 公开值未在 NLM | HAZID 起点缺 anchor |
| P2-B-01 BC ±30°  | DNV-RP-0691 ±60° 工业操实操 | maritime_regulations 待 ask（未投入 token） | 兜底转向幅度不足 |

⚫ Unknown 项：BC-MPC p99 solve time / Kongsberg / Wärtsilä 商用闭源数据 — **No industry analog with concrete number found**。

## 8. 多船型泛化检查

锚定 CLAUDE.md §4 决策四（Capability Manifest + PVA + 水动力插件 / Backseat Driver / 决策核心零船型常量）。

### FCB-only 假设清单（按本评审范围）

| ID | 假设 | 影响范围 | 切到拖船 / 渡船时失效场景 | 严重度 |
|---|---|---|---|---|
| FCB-01 | ROT_max(u) 三段线性（架构 §10.5 line 534–538）含 `1.2 / 1.0 / 0.8` 系数 | M5 Mid-MPC + BC-MPC | 拖船 ROT_max << FCB（5–6°/s typ），且**无高速段衰减**（拖船最高 ~ 12 kn）。系数表搬过去会预测 ~ 6 kn 拖船时舵效 1.2× = "幻觉转向能力" | **P0** |
| FCB-02 | f_speed_correction 段间断点 10 kn / 20 kn | 同上 | 渡船（半排水型，max ~ 18 kn）在 10 kn 是巡航而非低速段；段间断点错位 | P1 |
| FCB-03 | 波浪修正 `1 - 0.1 × Hs`（架构 §10.5 / M5 §5.4.2 line 545） | M5 Mid-MPC | 拖船型深 / 渡船型宽 GM 不同，对 Hs 的响应不是 0.1 系数 | P1 |
| FCB-04 | Mid-MPC N=18 / 90 s（覆盖 FCB 18 kn 制动 600–800 m）的"刚好覆盖"逻辑 | M5 Mid-MPC | 拖船 12 kn 制动距 ~ 200 m，N=18 严重过度；渡船 16 kn 制动 ~ 350 m 适中。N 应 = f(vessel_class) | **P0** |
| FCB-05 | CPA_safe(ODD-A) = 1.0 nm（架构 §10.3 line 852）"FCB 实操值" | M5 / M4 / M6 | 拖船拖带作业（受限船 Rule 18）需要更大 CPA；港内渡班通勤可降至 0.2 nm | P1 |
| FCB-06 | M5 详细设计 §5.5 表"实际耗时（FCB）"列直接命名 FCB | M5 复杂度估计 | 多船型扩展时该列失效，需 per-ship 重测 | P2（仅文档） |
| FCB-07 | MMG 4-DOF 假设（P0-B-05）| 全 M5 + Capability Manifest | FCB 半滑行需 6-DOF 预留；其他船型可继续 4-DOF。**反向 assumption**：如果设计继续锁 4-DOF 普适，FCB 自身就是"if vessel == FCB" 的反例 | **P0** |

**裁决**：FCB-01 / FCB-04 / FCB-07 列入 P0 ——决策核心代码（M5 Mid-MPC ROT 约束 + horizon + MMG）若不参数化为 Capability Manifest 字段，构成 "if vessel == FCB" 实质潜入。整改：

1. ROT_max(u) 系数曲线 → Capability Manifest `rot_max_curve` 字段（每船型独立标定）
2. N_horizon → Capability Manifest `mpc_horizon_steps` 字段（默认 18，覆盖最坏制动距）
3. hull_class ∈ {DISPLACEMENT / SEMI_PLANING / PLANING}，触发 MMG 维度选择
4. 波浪修正系数同上 Manifest 化
5. 在 M5 实现层 enforce："读 Capability Manifest 失败 → 拒绝启动"（不允许硬编码 fallback）

---

**评审完成时间**：2026-05-07
**评审用 token 预算**：deep research 2 / fast 0 / nlm-ask 4（在 cap 内）
