# 阶段 3 阅读顺序（v1.0 章节扫描）

> 策略：M7 (D3 Doer-Checker 独立性) 是 P0 级风险，先看；4 决策 (D1) 是基石，第二轮；
> 模块按依赖逆序（被依赖模块晚读）。
>
> Finding ID 格式：`F-{P0|P1|P2|P3}-D{1..9}-{seq:03d}`

## 章节映射（v1.0 实际章节 + 阅读顺序）

| 顺序 | 章节 | 主审维度 | 行范围（近似） |
|---|---|---|---|
| 1 | 第十一章 M7 Safety Supervisor | D3 Doer-Checker 独立性 | 738–810 |
| 2 | 第二章 核心架构决策 | D1 顶层决策完整性 | 83–131 |
| 3 | 第四章 系统架构总览 | D5 时间分层 | 237–314 |
| 4 | 第三章 ODD/Operational Envelope 框架 | D6 降级路径 + D1 决策一 | 132–236 |
| 5 | 第六章 M2 World Model | D4 接口契约（Multimodal Fusion 输入） | 422–508 |
| 6 | 第五章 M1 ODD/Envelope Manager | D6 + D1 决策一 | 315–421 |
| 7 | 第八章 M4 Behavior Arbiter | D9 IvP 选型 | 545–598 |
| 8 | 第十章 M5 Tactical Planner | D5 + D9 (Mid-MPC + BC-MPC 双层) | 655–737 |
| 9 | 第九章 M6 COLREGs Reasoner | D9 独立模块 vs 嵌入 | 599–654 |
| 10 | 第七章 M3 Mission Manager | D9 vs L1 Mission Layer 命名冲突 | 509–544 |
| 11 | 第十二章 M8 HMI/Transparency Bridge | D7 SAT 透明性 | 811–888 |
| 12 | 第十三章 多船型参数化设计 | D8 多船型兼容 | 889–968 |
| 13 | 第十四章 CCS 入级路径映射 | D7 入级证据 | 969–1026 |
| 14 | 第十五章 接口契约总表 | D4 接口契约横扫 | 1027–1107 |
| 15 | 第一章 背景与设计约束 | D1 背景兜底（FCB 特殊性） | 36–82 |

## Finding 草稿输出位置

所有阅读发现写入：`_temp/findings-raw.md`

格式：
```markdown
## F-P0-D3-001

**章节**：第十一章 §11.2  
**行**：780  
**论断**：...  
**问题**：...  
**置信度**：🔴  
**行动**：...
```

## 优先关注项（Phase 1 输入）

- §3.2 ODD 三轴定义（行 138–150）：R8 确认原文是二轴，需评估 v1.0 三轴改写是否合理
- §2.5 Doer-Checker 双轨（行 124–131）："Checker 简单 100× 以上"无来源，需 Phase 2 R4 补充或改措辞
- §14.2 DNV-CG-0264 9 子功能（行 975–990）：Phase 1 无法验证，需 Phase 2 R7 补充
- §9.3 T_standOn 阈值（行 636–645）：R17 标题不符，需 Phase 2 R2 提供替代来源
