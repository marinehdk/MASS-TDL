# 24 引用提取（含上下文）

> 来源：v1.0 §16 + grep 全文内联引用
> 每条含 full_citation（来自 §16）+ 典型 inline_locations（章节 + 行 + 论断）

---

```yaml
- ref_id: R1
  full_citation: |
    CCS《智能船舶规范》（2024/2025）+ 《船用软件安全及可靠性评估指南》. 中国船级社, 北京.
  type: 规范
  inline_locations:
    - section: §1.3 C1
      lines: 61
      claim: "系统须满足 CCS《智能船舶规范》入级要求，覆盖 i-Ship (Nx, Ri/Ai) 标志组合；决策逻辑须白盒可审计"

- ref_id: R2
  full_citation: |
    IMO MSC 110 (2025). Outcome of the Maritime Safety Committee's 110th Session. IMO MSC 110/23. London: International Maritime Organization. （含 MASS Code Chapter 15 草案"Proposed Amendments"MSC 110/5/1）
  type: 规范
  inline_locations:
    - section: §1.3 C2
      lines: 63
      claim: "系统须符合 MSC 110 (2025) 输出 + MSC 111 (2026-05) 即将通过的非强制版 MASS Code，特别是必须能识别自身越出 Operational Envelope"
    - section: §2.2
      lines: 95
      claim: "MASS Code 草案 §15 明确要求：系统必须能识别越出 Envelope，且独立于具体导航算法"
    - section: §12.4
      lines: 854
      claim: "责任移交（ToR）是 MASS Code 强制功能"

- ref_id: R3
  full_citation: |
    Benjamin, M.R., Schmidt, H., Newman, P.M., Leonard, J.J. (2010). Nested autonomy for unmanned marine vehicles with MOOS-IvP. Journal of Field Robotics, 27(6), 834–875.
  type: 学术
  inline_locations:
    - section: §1.3 C3
      lines: 65
      claim: "架构须支持多船型（FCB/拖船/渡船）参数化扩展（注：v1.0 §1.3 C3 把多船型约束 cite [R3]，但 R3 是 MOOS-IvP 论文——这是引用错位嫌疑）"
    - section: §8.3
      lines: 555
      claim: "IvP 多目标优化（源自 MOOS-IvP）的核心优势"
    - section: §13.3
      lines: 927
      claim: "三层解耦架构直接继承自 MOOS-IvP 的 Backseat Driver 范式"

- ref_id: R4
  full_citation: |
    Veitch, E., Alsos, O.A., Cheng, Y., Senderud, K., & Utne, I.B. (2024). Human factor influences on supervisory control of remotely operated and autonomous vessels. Ocean Engineering, 299, 117257. DOI: 10.1016/j.oceaneng.2024.117257
  type: 学术
  inline_locations:
    - section: §1.3 C4
      lines: 67
      claim: "ROC 操作员接管时窗 ≥ 60 秒（实证基础）"
    - section: §2.4
      lines: 122
      claim: "Veitch n=32 实证：可用接管时窗 (20s vs 60s) 与 DSS 可用性是 ROC 绩效最强影响因子"
    - section: §3.4
      lines: 166
      claim: "TMR 由 Veitch (2024) 实证确定，建议设计值 ≥ 60s"
    - section: §12.4
      lines: 878
      claim: "60 秒时窗来自 Veitch (2024) 实证数据"

- ref_id: R5
  full_citation: |
    IEC 61508-3:2010. Functional Safety of E/E/PE Safety-related Systems – Part 3: Software requirements. Geneva: International Electrotechnical Commission.
  type: 规范
  inline_locations:
    - section: §1.3 C5
      lines: 69
      claim: "核心安全功能须满足 IEC 61508 SIL 2 要求（注：行 69 中作 [R5, R6] 联引）"
    - section: §9.1
      lines: 607
      claim: "IEC 61508 可分性（separability）要求 → COLREGs 推理须独立可验证"
    - section: §11.1
      lines: 742
      claim: "ISO 21448 SOTIF + IEC 61508 双轨"

- ref_id: R6
  full_citation: |
    ISO 21448:2022. Road vehicles — Safety of the intended functionality (SOTIF). Geneva: ISO. （在本报告中用于海事 SOTIF 类比应用）
  type: 规范
  inline_locations:
    - section: §1.3 C5
      lines: 69
      claim: "感知降质 + 功能不足按 ISO 21448 SOTIF 处理"
    - section: §11.1
      lines: 742
      claim: "ISO 21448 SOTIF '功能不足'（functional insufficiency）概念"

- ref_id: R7
  full_citation: |
    Yasukawa, H. & Yoshimura, Y. (2015). Introduction of MMG standard method for ship maneuvering predictions. Journal of Marine Science and Technology, 20(1), 37–52. DOI: 10.1007/s00773-014-0293-y
  type: 学术
  inline_locations:
    - section: §1.4
      lines: 77
      claim: "MMG 标准方法的高速修正：半滑行船型 20 kn 以上旋回半径与速度平方成正比"
    - section: §10.6
      lines: 733
      claim: "MMG 标准方法（FCB 高速船型修正引用）"

- ref_id: R8
  full_citation: |
    Rødseth, Ø.J., Wennersberg, L.A.L., & Nordahl, H. (2022). Towards approval of autonomous ship systems by their operational envelope. Journal of Marine Science and Technology, 27(1), 67–76. DOI: 10.1007/s00773-021-00815-z
  type: 学术
  inline_locations:
    - section: §2.2
      lines: 95
      claim: "海事 ODD 必须含人机责任分配维度（区别于 SAE J3016），引入 TMR/TDL 量化参数"
    - section: §5.5（参考文献块）
      lines: 417
      claim: "Operational Envelope 三轴模型 + TMR/TDL 量化框架"

- ref_id: R9
  full_citation: |
    DNVGL-CG-0264 (2018). Autonomous and Remotely Operated Ships. DNV GL, Høvik.
    在线: https://maritimesafetyinnovationlab.org/wp-content/uploads/2020/09/DNVGL-CG-0264-Autonomous-and-remotely-operated-ships.pdf
  type: 规范
  inline_locations:
    - section: §2.3
      lines: 116
      claim: "DMV-CG-0264 §4 把导航分解为 9 个子功能（§4.2-§4.10）；本设计 8 模块对其 100% 覆盖"
    - section: §7.3
      lines: 540
      claim: "§4.2 'Planning prior to each voyage' — 独立子功能的规范要求"
    - section: §8.5
      lines: 595
      claim: "§4.5 'Deviation from planned route' + §4.8 'Manoeuvring'"

- ref_id: R10
  full_citation: |
    Urmson, C., Anhalt, J., et al. (2008). Autonomous driving in urban environments: Boss and the urban challenge. Journal of Field Robotics, 25(8), 425–466.
  type: 学术
  inline_locations:
    - section: §2.3
      lines: 116
      claim: "CMU Boss DARPA Urban Challenge 冠军 3 层规划栈：按职责而非算法划分模块是大型自主系统最佳实践"
    - section: §6.4
      lines: 503
      claim: "CMU Boss 'Perception & World Modeling' 独立子系统设计"
    - section: §7.3
      lines: 541
      claim: "CMU Boss 'Mission Planning' 层 — 独立任务计划层的工程先例"

- ref_id: R11
  full_citation: |
    Chen, J.Y.C., Procci, K., Boyce, M., Wright, J., Garcia, A., & Barnes, M. (2014). Situation Awareness-Based Agent Transparency (ARL Technical Report ARL-TR-7180). US Army Research Laboratory.
  type: 学术
  inline_locations:
    - section: §2.4
      lines: 122
      claim: "SAT 模型实证：SAT-1+2+3 全部可见时操作员态势感知提升且工作负荷不增加"
    - section: §12.2
      lines: 819
      claim: "M8 对 ROC 和船长呈现三层信息（SAT-1/2/3）"

- ref_id: R12
  full_citation: |
    Jackson, S.J., Clarke, F.M., et al. (2021). Certified Control: An Architecture for Verifiable Safety of Autonomous Vehicles. arXiv:2104.06178.
  type: 学术
  inline_locations:
    - section: §2.5
      lines: 128
      claim: "Certified Control 框架从形式验证角度论证 Doer-Checker 分离的必要性；Checker 规约必须足够简单以支持形式化证明"

- ref_id: R13
  full_citation: |
    Albus, J.S. (1991). Outline for a theory of intelligence. IEEE Transactions on Systems, Man, and Cybernetics, 21(3), 473–509. NIST Real-time Control System (RCS) Reference Architecture.
  type: 学术
  inline_locations:
    - section: §6.4
      lines: 504
      claim: "Albus NIST RCS（参考体系结构）各节点独立 World Model 原则"

- ref_id: R14
  full_citation: |
    Fjørtoft, K. & Rødseth, Ø.J. (2020). Using the Operational Envelope to Make Autonomous Ships Safer. Proceedings of the NMDC 2020, Ålesund, Norway. (Semantic Scholar CorpusID: 226236357)
  type: 学术
  inline_locations:
    - section: §5.5（参考文献块）
      lines: 418
      claim: "'Using the Operational Envelope to Make Autonomous Ships Safer'"

- ref_id: R15
  full_citation: |
    MUNIN Consortium (2015). MUNIN D5.2: Advanced Sensor Module (ASM) Design. FP7 Project 314286. Fraunhofer CML, Hamburg.
  type: 工业
  inline_locations:
    - section: §6.4
      lines: 505
      claim: "MUNIN FP7 D5.2 Advanced Sensor Module (ASM) 设计"

- ref_id: R16
  full_citation: |
    Pirjanian, P. (1999). Behavior coordination mechanisms – State-of-the-art. USC Computer Science Technical Report IRIS-99-375. USC Robotics Research Labs.
  type: 学术
  inline_locations:
    - section: §8.2
      lines: 551
      claim: "传统优先级仲裁缺陷：固定优先级会导致低优先级行为零贡献"
    - section: §8.5
      lines: 594
      claim: "'Behavior Coordination Mechanisms' — 优先级仲裁缺陷分析"

- ref_id: R17
  full_citation: |
    Wang, T., Zhao, Z., Liu, J., Peng, Y., & Zheng, Y. (2021). Research on Collision Avoidance Algorithm of Unmanned Surface Vehicles Based on Dynamic Window Method and Quantum Particle Swarm Optimization. Journal of Marine Science and Engineering, 9(6), 584. DOI: 10.3390/jmse9060584
  type: 学术
  inline_locations:
    - section: §9.3
      lines: 641
      claim: "T_standOn 阈值 8/6/10 min 来自 Wang et al. 2021 直航船四阶段定量分析"
    - section: §9.3
      lines: 642
      claim: "T_act 独立避让阈值 4/3/5 min"

- ref_id: R18
  full_citation: |
    IMO (1972/2002). Convention on the International Regulations for Preventing Collisions at Sea, 1972 (COLREGS), as amended. London: IMO.
  type: 规范
  inline_locations:
    - section: §9.4
      lines: 648
      claim: "COLREGs 1972 现行版 Rule 5, 6, 7, 8, 9, 13–19"

- ref_id: R19
  full_citation: |
    Bitar, G.I., Breivik, M., & Lekkas, A.M. (2019). Hybrid Collision Avoidance for ASVs Compliant with COLREGs Rules 8 and 13–17. arXiv:1907.00198.
  type: 学术
  inline_locations:
    - section: §9.4
      lines: 650
      claim: "Bitar et al. 2019 — COLREGs 状态机分离可验证设计"

- ref_id: R20
  full_citation: |
    Eriksen, B.H., Bitar, G., Breivik, M., & Lekkas, A.M. (2020). Hybrid Collision Avoidance for ASVs Compliant With COLREGs Rules 8 and 13–17. Frontiers in Robotics and AI, 7:11. DOI: 10.3389/frobt.2020.00011
  type: 学术
  inline_locations:
    - section: §10.4
      lines: 708
      claim: "BC-MPC 采用 Eriksen 等 (2020) 分支树算法"
    - section: §10.6
      lines: 732
      claim: "Eriksen, Bitar, Breivik et al. (2020) Frontiers — BC-MPC 算法原理"

- ref_id: R21
  full_citation: |
    Yasukawa, H. & Sano, M. (2024). MMG 4-DOF extended model for asymmetric inshore flow correction. Journal of Marine Science and Technology. (引用于 Patch-B §6.10 注记)
  type: 学术
  inline_locations:
    - section: §10.5
      lines: 728
      claim: "Hs > 1.5m 引入波浪扰动项，参照 Yasukawa & Sano 2024 近岸修正"

- ref_id: R22
  full_citation: |
    Neurohr, D., et al. (2025). Towards Efficient Certification of Maritime Remote Operations Centres via ISO 21448 SOTIF. arXiv:2508.00543.
  type: 学术
  inline_locations:
    - section: §11.5
      lines: 807
      claim: "SOTIF 在海事 ROC 认证中的应用"

- ref_id: R23
  full_citation: |
    Veitch, E., & Alsos, O.A. (2022). "From captain to button-presser: operators' perspectives on navigating highly automated ferries." Journal of Physics: Conference Series, 2311(1). NTNU Shore Control Lab.
  type: 学术
  inline_locations:
    - section: §12.5
      lines: 885
      claim: "NTNU Shore Control Lab — 从'船长'到'按钮操作员'的认知退化研究"

- ref_id: R24
  full_citation: |
    Sea Machines Robotics (2024). SM300-NG Class-Approved Autonomous Command System. 在线: https://sea-machines.com/sm300-ng/
  type: 工业
  inline_locations:
    - section: §13.3
      lines: 931
      claim: "Sea Machines SM300 商业化 TALOS + SMLink Control-API，已在 200+ 艘船 (20 国) 部署，是当前最强工业验证"
```

---

## 统计

- 规范类（A 级）：R1, R2, R5, R6, R9, R18 = **6 条**
- 学术类：R3, R4, R7, R8, R10, R11, R12, R13, R14, R16, R17, R19, R20, R21, R22, R23 = **16 条**
- 工业类：R15, R24 = **2 条**
- **总计**：24 条

## 已发现的可疑点（先记下，subagent 验证时重点关注）

1. **R3 引用错位嫌疑**：v1.0 §1.3 C3 把"多船型兼容约束"cite [R3]，但 R3 实际是 Benjamin et al. 2010 MOOS-IvP 论文——MOOS-IvP 是 Backseat Driver 范式，与多船型兼容相关但不直接论证"船型扩展"。需要验证 R3 在 §1.3 是否真支持该论断
2. **R12 论断强度**：v1.0 §2.5 用 R12 (Jackson 2021 Certified Control) 论证 Doer-Checker "100×简单"——但 Jackson 论文是否给出"100×"这个量化数字？需要验证
3. **R8 三轴定义**：v1.0 §3.2 用 R8 (Rødseth 2022) 定义 Operational Envelope 为 E×T×H 三轴。原文是否真用"三轴"？还是 v1.0 改写
