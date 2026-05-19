# F 评审报告 — 架构 & 模块完整性

| 字段 | 值 |
|---|---|
| Reviewer Persona | 系统架构师（DNV / ABS 验证官风格）|
| Scope | 架构 v1.1.2 全文 + 8 模块详细设计草稿 + RFC-decisions + 99-followups |
| 总体判断 | 🟡（结构骨架可启动；但 v3.0 工业基线轴向多个 seam 仍未规范化，存在 4 个 P0 阻断项）|
| 评审基线 | 架构 v1.1.2 / RFC-decisions v1.0 / Detailed Design v0.x |
| 评审时间 | 2026-05-07 |

---

## 1. Executive Summary（≤200 字）

v1.1.2 架构在 8 模块功能边界、Doer-Checker 双轨、SAT 透明性、ODD 三层框架等四项顶层决策上**已达到正式设计稿的一致性**。RFC-001~006 关闭了与 L4 / Fusion / X-axis / ASDR / Reflex / L2 的 6 条接口缺口。但作为系统架构师，我看到 4 类未关闭的 seam：(i) **L3 内部网络安全空白**——IACS UR E26/E27 仅出现 1 次（§14.4 测试程序），M8 → ROC、M2 ← Fusion、ASDR 写入均未定义鉴权 / 完整性 / DDS-Security profile；(ii) **时间同步 master 未指定**——IDL 普遍要求 `stamp` 但全文无 PTP/NTP/sync-error 容差，4 Hz/50 Hz/100ms 等实时承诺缺底层时基；(iii) **Capability Manifest 抽象有 2 处泄漏**（M5 §7 FM-4 hardcoded `ROT_max=8°/s` fallback、M2 §2.2 `[0,30] kn / FCB 满载`），违反决策四；(iv) **环境输入 FMEA 缺失**——风/浪/流 / ENC 仅 "丢失 30 s 用缓存"，无错误数据 / 漂移 / 跨源矛盾的检测路径。M3 角色在 v1.1.2 已收窄为 "本地 tracker"，但与 L1 边界仍模糊。

---

## 2. P0 阻断项（4 条）

### P0-F-01 · L3 边界缺乏网络安全规范（IACS UR E26/E27 合规真空）

**Finding**：IACS UR E26 / E27 自 2024-07-01 强制生效于新造船 [W1]，覆盖 **OT 网络分区 / CBS 安全能力 / 连接到不可信网络的限制**。架构文档全文 "网络安全" 仅出现一次 — §14.4 表格中"网络安全测试程序 → IACS UR E26/E27 合规"作为 M2–M3 阶段交付物 [arch §14.4 行 1371]。但：
- §15 接口契约总表 21 行接口，**无任一行声明鉴权方式 / 完整性保护 / 重放保护**
- M8 → ROC `UI_StateMsg` / `ToR_RequestMsg` 跨越 DMZ 但 M8 详细设计仅出现 SHA-256 用于 ASDR 内部签名，未出现 TLS / mTLS / DDS-Security 关键字 [M8 detailed design grep]
- M2 ← Multimodal Fusion 三话题 (`/perception/targets` / `/nav/filtered_state` / `/perception/environment`) 未定 DDS-Security profile，亦未定义 Fusion 失陷时 M2 的退化策略
- ASDR `bytes signature` 仅 SHA-256（无 key），RFC-004 决议明示 "HMAC 留 v1.2 安全升级" — 此为已知 gap [RFC-decisions.md:78]

**影响**：CCS i-Ship (Nx, Ri/Ai) 入级评审中 IACS UR E26 是硬条件 [W1]。当前架构在 **detect / protect** 两个功能元素上对 L3 内部 / L3 ↔ Fusion / L3 ↔ Shore Link 三个 trust boundary 的设计为零。HAZID 阶段不会自动覆盖 cyber，必须在架构上补章节。

**证据**：
- 架构 §14.4 table 行 1371（仅一次提及）
- RFC-decisions.md:78（HMAC 推迟）
- W1: [IACS UR E26/E27](https://iacs.org.uk/news/iacs-adopts-new-requirements-on-cyber-safety/)（Rev.1 entry-into-force 2024-07-01）🟢
- W2: [Pen Test Partners — E26/E27 trust boundary 限制](https://www.pentestpartners.com/security-blog/iacs-ur-e26-and-e27-guidance/) — "all systems certified to E27 by ClassNK as of Nov 2024 state they cannot be connected to untrusted networks" 🟢

**整改建议**：
- v1.1.3 新增 §16 "L3 网络安全设计"（章节级，与 §14 CCS 映射对称）：
  1. trust zone 划分（M8 ↔ ROC = untrusted；M2 ↔ Fusion = trusted；ASDR ↔ Shore Link = mixed via gateway）
  2. 每条 §15.2 接口加 4 列：`auth | integrity | replay-protection | dds-security profile`
  3. M8 详细设计补 ToR 链路 mTLS + 操作员令牌签名（替代现有的"按钮点击 → ASDR JSON"裸协议）
- Owner：架构师 + M8 负责人；关闭日期：5 月开工前必须先有 §16 stub，3 周内补全

**置信度**：🟢（W1+W2+arch grep 三源一致）

---

### P0-F-02 · 全系统时间同步主控（time master）未定义

**Finding**：架构 §15.1 全部 16 个 IDL 消息均含 `timestamp stamp` 字段；§15.2 列出 1 Hz / 4 Hz / 10 Hz / 50 Hz 的频率承诺；§11.9 RFC 锁定 100 ms 时延、< 50 ms 接管冻结、§11.3 锁定 "100 周期 = 15 s 滑窗"。但通读全文 + grep "PTP/NTP/sync/master/drift/时钟" 仅 1 命中（M2 §3.1 行 136 "计时源与系统时钟同步"，无 master 指定）[grep result]。

具体问题：
1. M2 World_StateMsg 4 Hz 聚合 Fusion 50 Hz + 2 Hz + 0.2 Hz 三话题，**插值/外推须共享时基** — 但未定义 master clock 是 Fusion / M1 / Nav Filter / 外部 PTP grandmaster
2. CPA/TCPA 在 M2 计算（RFC-002），输入 own_ship 与 target 时间戳分别来自 50 Hz nav filter 与 2 Hz fusion — 时间对齐误差 250 ms 时 CPA 误差对 18 kn FCB ≈ 2.3 m，TCPA 阈值校准的有效性受冲击
3. M7 RFC-003 锁定的 "100 周期 = 15 s" 需要稳定的 6.7 Hz 周期，但 M7 周期源未定
4. ASDR SHA-256 签名 + 跨模块决策追溯需要全船统一时基才能复盘——CCS i-Ship 白盒可审计要求

**影响**：所有时序 + 时延 SLA + ASDR 重建 + CPA/TCPA 计算的可信度失去基线。CCS / DNV 验证官在审查"实时性能"证据时会要求 PTP master + sync error budget 文档。

**证据**：
- arch §15 IDL 全部 16 条消息
- M2 detailed design 行 136 唯一一处提及（无 master）
- W1 IACS UR E26 §"Detect" 需要时序日志可信 [https://iacs.org.uk/news/iacs-adopts-new-requirements-on-cyber-safety/] 🟢
- 工业先例：Kongsberg K-MATE / Yara Birkeland 文档已在 [W3] DNV CG-0264 §9.x 提到 IEEE 1588 PTP（待 NLM 调研补 🟡 → 🟢）

**整改建议**：
- v1.1.3 §15.0 新增"时基与同步"小节：
  - 指定 IEEE 1588 PTPv2 grandmaster（推荐位于 Multimodal Fusion 子系统内或独立 PTP appliance）
  - 给出 sync error budget：boundary clock 间 ≤ 100 µs；终端节点 ≤ 1 ms
  - §15.1 IDL 消息 `stamp` 字段语义明示（PTP-derived，TAI 或 UTC）
  - 退化路径：PTP 失锁 → NTP fallback；NTP 失锁 → M7 触发 SOTIF "时基不可信"告警 → 降级至 D2
- Owner：架构师 + Multimodal Fusion 跨团队；关闭日期：v1.1.3（HAZID 后）

**置信度**：🟡（grep 证据高；最佳工业实践 PTP 普遍使用，但本仓库 NLM 未直接核验 K-MATE 文档使用 PTP — 标 🟡 到补调研）

---

### P0-F-03 · Capability Manifest 抽象层泄漏（决策四违反）

**Finding**：CLAUDE.md §4 决策四 / arch §13 "决策核心零船型常量"。但 grep 出 2 处明确违反：

1. **M5 §7.2 FM-4 fallback**：`Fallback ROT_max = 8°/s（极保守）` [M5 detailed design:674]
   - 这是 **M5 代码内 hardcoded 数值**，触发条件是 "Capability Manifest 数据坏"
   - 拖船（ROT 极限可能 < 5°/s）/ 70m PSV（ROT < 3°/s）执行此 fallback 反而**激进**，违反"保守"语义
   - 正确做法：从 Manifest schema 中要求 `rot_max_fallback` 为强制字段；Manifest 加载失败 = `OUT_of_ODD` 触发 MRC，而非用一个数字硬编码

2. **M2 §2.2 校验**：`sog ∈ [0, 30] kn；u,v ∈ [-15, 15] m/s（FCB 满载极限）` [M2 detailed design:48]
   - `(FCB 满载极限)` 注解承认了硬编码事实
   - 拖船目标在港内偶尔会低于 1 kn → 当前阈值不会误报；但若引入高速渡船自船（u,v > 15 m/s = 29 kn）→ **自身状态会被 M2 标 INVALID 并触发 M7 故障告警**
   - 正确做法：阈值来自 `Capability Manifest.envelope.max_speed_kn × 1.2`

3. **M5 §10.3 + §11.6 MRM 参数**：4 kn / 30s / ±60° / 30 m / 4 kn 抛锚 — v1.1.1 已加 [HAZID 校准]，但 **未声明这些值是 FCB-only 还是 vessel-agnostic 模板**。多船型扩展时整张表是不是要为每船型单独 HAZID 一次？文档未说

**影响**：直接破坏 v1.0 → v1.x 的核心抽象承诺；首船型扩展（拖船 / 渡船）会触发"if vessel == FCB" 风险。

**证据**：
- M5 detailed design 行 674（FM-4 fallback）
- M2 detailed design 行 48（sog 校验）
- arch §11.6 MRM 表（无 vessel-agnostic 声明）
- CLAUDE.md §4 决策四
- 工业先例 [R3] MOOS-IvP Backseat Driver / [R24] Sea Machines SM300 — 决策核心 vessel-agnostic 是 200+ 船部署的前置条件 🟢

**整改建议**：
1. M5 §7.2 FM-4：把 `ROT_max = 8°/s` 改为：`抛出 CRITICAL → M1 → MRM-02 (停车)`；删除 hardcoded 数字
2. M2 §2.2 校验阈值改为 `f(Manifest.max_speed)`，并补 unit test "loading 拖船 manifest → 阈值自适应"
3. arch §13 + §11.6 加表格列：`参数适用范围`（FCB-only / vessel-template / universal）
4. 加一条 lint 规则：架构师 / 模块 owner PR review 必须 grep `FCB|18 kn|22 kn|45m` 在决策核心代码区域为 0
- Owner：M5 + M2 + 架构师；关闭日期：5 月开工前必须 fix M5 + M2 两处；§13 表格 v1.1.3

**置信度**：🟢（grep + CLAUDE.md 三源一致）

---

### P0-F-04 · 环境输入 FMEA 缺失（风/浪/流 / ENC / 能见度）

**Finding**：架构 §3.3 ODD 子域以风浪流 / 能见度为分类轴；§5.4 M1 订阅 `传感器状态(能见度/海况/GPS质量)` 作为 ENV_EVAL 输入。但：

1. **EnvironmentState 错误数据无检测路径**：M2 §2.2 校验表 6 项校验全是 own_ship + target；环境字段（visibility / Hs / current）**没有任何 sanity check**
2. **风/流估计错误的级联效应未分析**：FilteredOwnShipState 提供 `current_speed/direction`，M3 §7 表"海流估计异常 (>3 kn 误差) → 降级 confidence；发 ASDR 警告"中等优先级 [M3:718] — 但 M5 Mid-MPC ETA 投影 + COLREGs T_standOn / T_act 都依赖海流，3 kn 误差可使 18 kn FCB 在 8 min T_standOn 窗口位置偏差 ≈ 740 m，**直接破坏 COLREG 时机判定**
3. **ENC 数据库刷新管理无模块**：M2 SV 视图依赖 ENC，但 ENC 更新（每周 / 每航次）由谁触发、ENC 数字签名验证、过期 ENC 处理路径 → 无 owner。M5 §7.2 FM-5 仅说 "TSS 多边形数据过期 > 10 min → 禁用 TSS 约束" — 这把过期检测放在 M5 局部，无系统级 chart manager
4. **环境跨源矛盾未处理**：Multimodal Fusion 给的 visibility 与 ENC METAR 可能矛盾，ODD 决策应 trust 哪个？无规范

**影响**：环境是 ODD 的 E 轴主成分，环境感知错误直接导致 M1 在错误 ODD 子域下决策 → 全栈级误。SOTIF（ISO 21448）要求"传感器输入失效"是核心触发条件分析对象，当前架构对环境通道近乎透明。

**证据**：
- arch §3.3 子域定义；§5.4 M1 输入
- M2 detailed design §2.2（grep 显示无环境字段校验）
- M3 detailed design 行 718（海流误差仅"中"等级）
- M5 detailed design 行 675（FM-5 ENC 过期局部处理）
- 工业先例：DNV-CG-0264 §6 / §7 要求 sensor input integrity FMEA [R9] 🟢
- ISO 21448 §8 SOTIF triggering conditions 把 environmental degradation 列为 mandatory analysis [R6] 🟢

**整改建议**：
- v1.1.3 §6.5 新增 "环境输入 FMEA"（M2 章节内）：每个环境字段定义 (range_check / cross_source_check / staleness_check / fallback)
- 新增 §13.x "ENC Manager"（或委托 L2 但显式 IDL 化）— 章节级补全 chart 更新流程
- M3 海流误差等级从"中"提升到"高"（直接影响 COLREG 时机）
- M7 §11.3 SOTIF 假设清单增加 "环境输入跨源一致性" 行
- Owner：M2 + M3 + M7 + 架构师；关闭日期：v1.1.3

**置信度**：🟢（grep + ISO 21448 + DNV-CG-0264 三源一致）

---

## 3. P1 重要风险（6 条）

### P1-F-01 · M3 角色 vs L1 任务层边界仍模糊

v1.1.2 §7 把 M3 显式缩为 "Local Mission Tracker"，但 §15.2 接口矩阵仍把 `VoyageTask` 来自 "L1 Voyage Order" 直接订阅到 M3 [arch:1602]。在 v3.0 baseline 中 L1 = Mission Layer (hrs~days)，应由 L2 消费 VoyageTask 后再生成 PlannedRoute / SpeedProfile 给 L3。当前等于 M3 同时订阅 L1 + L2，**绕过了 L2 抽象**。证据：CLAUDE.md §2 系统坐标系 / arch §1.2 / arch §15.2 行 1602。建议：v1.1.3 决议 — 要么 L1 → L2 → M3 单链；要么明确双订阅是出于 ETA 投影需要并加跨层独立性论证。🟢

### P1-F-02 · M4 Mission Manager 与 M3 命名混淆遗留

arch §1.2 / §13 仍多处用 "Mission Manager" 描述 M3。CLAUDE.md §3 表把 M3 列为 "Mission Manager / 航次计划、ETA、重规划触发" — 与 v1.1.2 §7 "Local Mission Tracker" 收窄不符。审计上这是术语不一致。建议 v1.1.3 全文搜索替换 + CLAUDE.md §3 同步更新。证据：arch §7 vs CLAUDE.md §3。🟢

### P1-F-03 · Reflex Arc / Override / Checker 与 L3 优先级仲裁仅 RFC-005 一处

RFC-005 §6 锁定 "Hardware Override > Reflex Arc > M5 ReactiveOverride > M5 AvoidancePlan > L4 LOS"。但架构主文档 §11.7 / §11.9 / §10.7 各处只描述 L3 内部冻结，**仲裁矩阵未进入 §15 主表**。v1.1.3 应把仲裁优先级矩阵升入架构主文档单独小节，而非散落于 RFC。证据：RFC-decisions.md:95 vs arch §15。🟡

### P1-F-04 · ASDR 写入路径无 schema 版本字段

`ASDR_RecordMsg.decision_json` 是 string，未带 `schema_version` 字段。CCS 多年后回放需要兼容旧 schema；M5/M6/M7 schema 升级会断层。建议 IDL 加 `string schema_version`。证据：arch §15.1 ASDR_RecordMsg。🟢

### P1-F-05 · M7 心跳由 X-axis 监控但无 X-axis spec

§11.8 "M7 心跳由 X-axis Deterministic Checker 监控"。X-axis 是其他团队产物（"只读"）；未来 X-axis 团队是否承诺 1 Hz 心跳订阅？无 RFC 决议。建议追加 RFC-007（M7 ↔ X-axis 心跳契约）。证据：arch §11.8 + RFC-decisions.md 仅 6 RFC。🟢

### P1-F-06 · §15 IDL 缺 schema 版本与 backwards-compat 政策

全部 16 条消息无 `schema_version` 字段；架构未声明字段增减的兼容性规则（如 Protobuf 风格 reserved tag）。8 团队并行实现 → schema drift 风险高。证据：arch §15.1。🟢

---

## 4. P2 改进建议（4 条）

### P2-F-01 · §15.2 缺 QoS（reliability / durability / deadline）

DDS QoS 是"频率承诺能否真的达到"的实现要件。每行加 4 列 (`reliability / history / deadline / lifespan`) 立刻提升可实现性。🟡

### P2-F-02 · M8 ↔ ROC 50 Hz 推送的网络与 cyber 联合分析缺

50 Hz UI 推送跨 DMZ 是 P0-F-01 的延伸；同时 50 Hz × 全 SAT 字段 → 带宽估算缺。建议在 M8 §x 加 bandwidth budget。🟡

### P2-F-03 · Capability Manifest 自定义规范无 schema 版本与签名链

§13.1 声明 "CCS 签名验证"，但未给签名链（CCS root → vessel manifest → plugin .so）。v1.1.3 §13 加签名链图示。🟡

### P2-F-04 · 缺少 "calibration manager / param store" 模块

HAZID 校准的 25+ 参数（附录 E）部署后由谁写入哪里？arch / 详设均未指明。建议 v1.1.3 §13 加 "Parameter Store" 子模块（vessel-agnostic，hot-reload + signed delta）。🟡

---

## 5. 架构 / 模块缺失项（候选新模块）

| 缺失模块 / 子模块 | 理由 | 工业先例 |
|---|---|---|
| **§16 Cybersecurity Spec**（与 §14 平级） | P0-F-01 — IACS UR E26/E27 合规 | DNV CIP / Kongsberg K-Bridge cyber 章节 [W2] |
| **§15.0 时基同步** 或 Time Authority 子模块 | P0-F-02 — 全系统 PTP master | Kongsberg / Yara Birkeland（待补 NLM 核验）🟡 |
| **ENC / Chart Update Manager** | P0-F-04 — chart 生命周期 | ECDIS Type-Approved chart manager（IMO MSC.232(82)）🟢 |
| **Parameter Store / Calibration Manager** | P2-F-04 — HAZID 参数 hot-reload | Sea Machines TALOS Parameter DB [R24] 🟡 |
| **Environment Cross-Source Validator**（M2 子模块或独立）| P0-F-04 — Fusion / ENC / METAR 矛盾 | DNV-CG-0264 §7 sensor input integrity 🟢 |

---

## 6. 调研引用清单

**仓库内**：
- `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md` v1.1.2 §1–§16 + 附录 A–D''+E
- `docs/Design/Detailed Design/M{1-8}/01-detailed-design.md`（M1:264, M2:48/136/680, M3:38/41/86/203-207/581-586/618/695/714/718/760-772, M5:270/674-676, M6:435/440/529/1244-1265, M7:1145/1646, M8:614/632/654）
- `docs/Design/Cross-Team Alignment/RFC-decisions.md`（RFC-001~006）
- `docs/Design/Architecture Design/audit/2026-04-30/99-followups.md`（P3 deferred）
- CLAUDE.md §2 / §3 / §4

**Web**：
- W1: [IACS UR E26/E27 Adoption Press Release](https://iacs.org.uk/news/iacs-adopts-new-requirements-on-cyber-safety/) 🟢 A 级官方
- W2: [Pen Test Partners — IACS UR E26/E27 Guidance](https://www.pentestpartners.com/security-blog/iacs-ur-e26-and-e27-guidance/) 🟢 B 级专业
- W3: [Inmarsat Whitepaper — IACS E26/E27 Beyond Compliance, June 2024](https://www.inmarsat.com/content/dam/viasat-maritime/perspectives/documents/MBU_IACS_UR_E26_E27_whitepaper_June_2024.pdf) 🟢 B 级行业
- W4: [ClassNK UR E26/E27 implementation](https://www.classnk.or.jp/hp/en/activities/cybersecurity/ur-e26e27.html) 🟢 A 级 IACS member

**NLM**：本次评审**未触发** /nlm-research（4 条 P0 证据链已用 web + arch grep 三源对齐到 🟢/🟡）。建议在 v1.1.3 spec 阶段对以下查询走 `--depth deep --add-sources`：
- safety_verification: "IACS UR E27 onboard CBS DDS-Security profile autonomous ship"
- maritime_regulations: "PTP IEEE 1588 maritime onboard time synchronization Kongsberg Yara reference"

## 6.x 建议新增 DOMAIN 笔记本

- **名称**：DOMAIN · Maritime Cybersecurity & Onboard Networks
- **关键词**：IACS UR E26/E27/E22, OT network zoning, DDS-Security, mTLS shore link, ECDIS chart signing, ASDR HMAC chain
- **触发原因**：P0-F-01 + P0-F-02 + P2-F-02 + 架构 §16 缺失 — 现有 5 个 DOMAIN（safety_verification / ship_maneuvering / maritime_human_factors / maritime_regulations / colav_algorithms）均无良好归属。maritime_regulations 偏向 IMO/COLREGs/CCS，cyber 是独立大类。

---

## 7. 行业基准对标（Industry Benchmark）

| 选型 / 做法 | 本设计 | 对标对象 | 差距 / 证据 |
|---|---|---|---|
| L3 内部 cyber 章节 | ❌ 无 | Kongsberg K-Bridge cyber spec / DNV CIP | P0-F-01；W1+W2 🟢 |
| 全系统时基 master | ❌ 无 | IEEE 1588 PTPv2（航空/陆地自动驾驶通用） | P0-F-02；🟡（NLM 待核 K-MATE 是否用 PTP）|
| Capability Manifest 抽象 | ✅ §13 三层解耦 | MOOS-IvP Backseat Driver [R3] / Sea Machines TALOS [R24] | 抽象方向正确，但 M5 / M2 有 2 处泄漏（P0-F-03）🟢 |
| Doer-Checker 双轨 | ✅ §11 | Boeing 777 PFCS / Airbus FBW COM/MON / NASA Auto-GCAS | RFC-003 enum 化 + 100 周期窗口锁定 🟢 |
| MRM 命令集 | ✅ §11.6 4 命令 | Sea Machines SM300 / DNV-CG-0264 §9.8 | 缺 vessel-agnostic 声明 (P0-F-03) 🟢 |
| ASDR 决策追溯 | ✅ §15 | IMO MASS Code Part 2 (Extended VDR) | 缺 schema_version + HMAC（P1-F-04 + RFC-004 deferred）🟢 |
| 环境输入 FMEA | ❌ 几乎无 | ISO 21448 §8 + DNV-CG-0264 §7 | P0-F-04 🟢 |
| ENC chart manager | ❌ 无独立模块 | IMO MSC.232(82) ECDIS Type Approval | P0-F-04 / 缺失模块 🟢 |

**No industry analog found**：M3 "Local Mission Tracker" 同时订阅 L1 VoyageTask + L2 PlannedRoute 的双订阅模型 — 工业先例（Kongsberg / Sea Machines）多为 L2 单链；建议补论证或退回单链（P1-F-01）。⚫

---

## 8. 多船型泛化检查（Multi-vessel Generalization）

> 本 angle 是 §6 cross-cutting 的最重点。结果：发现 2 个明确 P0 + 1 个 P1 泄漏。

### FCB-only 隐含假设清单

| # | 假设 | 文件:行 | 影响 | Severity |
|---|---|---|---|---|
| 1 | M5 §7.2 FM-4 fallback `ROT_max = 8°/s` 硬编码 | `docs/Design/Detailed Design/M5-Tactical-Planner/01-detailed-design.md:674` | 拖船 / 70m PSV 在此 fallback 下不安全（其物理 ROT_max < 8°/s） | **P0**（P0-F-03） |
| 2 | M2 §2.2 校验 `sog ∈ [0,30] kn；u,v ∈ [-15,15] m/s（FCB 满载极限）` | `docs/Design/Detailed Design/M2-World-Model/01-detailed-design.md:48` | 高速渡船自身被标 INVALID | **P0**（P0-F-03） |
| 3 | M5 §10.3 默认 `N=12, Δt=5s, T=60s` 等参数无 vessel-agnostic 声明，即使 [HAZID 校准] 注释也未说明每船型独立校准 | `M5/01-detailed-design.md:270, 1225` | 多船型扩展时 HAZID 范围不明 | **P1** |
| 4 | arch §11.6 MRM 表全表 [初始设计值，HAZID 校准] 但未声明 "vessel-template per Capability Manifest" 含义 | `arch §11.6` | 拖船 / 渡船 MRC 直接套 FCB 值会语义错误 | **P1** |
| 5 | M2 §2.2 时间漂移阈值 250 ms = 4 Hz 假设；其他船型若 M2 频率不同则不可移植 | `M2/01-detailed-design.md:52` | M2 频率作为 vessel parameter 是合理但未声明 | P2 |

### 建议处理

1. M5 / M2 两个 P0 在 5 月开工前必须 fix（详 P0-F-03 整改）
2. arch §13 加表"参数适用范围"列；§11.6 / §10.3 / §9.3 三表全部补 `(FCB-baseline, vessel-template, universal)` 标签
3. PR review checklist 加一条："grep `FCB|45m|18 kn|22 kn|ROT_max\s*=\s*\d` 在 A 层（M1–M8 决策核心）必须为 0"

---

## 修订记录

| 版本 | 日期 | 评审人 | 变更 |
|---|---|---|---|
| v1.0 | 2026-05-07 | F-architect persona | 初版（4 P0 / 6 P1 / 4 P2 + 5 缺失模块）|
