# 船长访谈协议 · D2.6

| 版本 | 2026-06-16 v1.0 |
|---|---|
| 用途 | HF 咨询 onboard 后直接使用，每次访谈约 50 min |
| 目标 | 建立 SAT-2 决策透明性理解度基线；校准 M8 §12.3 差异化视图假设 |

## 参与者标准

| 优先级 | 来源 | 最低要求 |
|---|---|---|
| ① | FCB 在职船长 / 大副（首选）| ≥2 年近岸运营经验 |
| ② | 外部资深船长（航海院校 / 船东协会 / HAZID 干系人）| ≥OOW 证书 + ≥5 年当值经验 |
| ③ | R/V Gunnerus 公开访谈资料（NTNU）| 仅补足数量，不替代签字来源 |

目标：n=5（FCB-01/02/03 + TUG-01 + FERRY-01），匿名化 ID 见 interview-{01..05}.md。

**招募 Checkpoint（风险 R2.E 触发判断点）：** 访谈窗口开始前 7 天（即 **2026-06-09**）确认已锁定 ≥5 人名单。
- 若 6/9 仅确认 ≤3 人 → 立即启动 fallback ②，PM 当日联系航海院校 / 船东协会
- 若 6/9 仍缺 2+ 人且 fallback ② 无进展 → 启动 fallback ③ Gunnerus 资料补位（HF 咨询核实资料充分性）

## 访谈流程（总 ~50 min）

| 段落 | 时长 | 形式 | 内容 |
|---|---|---|---|
| 暖场 | 5 min | 非正式 | 介绍研究目的；签署知情同意书 |
| 半结构化 | 30 min | 访谈员引导 | Topic Guide T1–T5（见下）|
| Think-aloud | 15 min | 受访者主导 | SAT-2 刺激材料（见下）|
| 收尾 | 5 min | 问卷 | 透明性理解度问卷填写 |

## Topic Guide（半结构化部分）

```
T1. 常规航行（Transit）：您最频繁检查的信息是什么？检查顺序？
T2. 目标船接近（COLREGs 场景）：您第一眼看哪里？多久关注一次？
T3. 异常 / 接管决策：有没有您希望系统"早点告诉你"的信息？格式是？
T4. 系统辅助经历：如果有自动避让系统，您想知道它"为什么这样做"到什么程度？
T5. ROC 视角（如适用）：岸基操作员和桥楼船长对信息需求有何不同？
```

## Think-aloud 刺激材料

**主选：** D1.3.2.3 Web HMI + Imazu head-on 场景（DEMO-1 2026-06-15 后稳定版本）
- 展示 SAT-2 colregs_chain topic（5 层规则链可见）
- 请受访者实时说出："你先看哪里？你理解系统为何这样动作吗？"

**降级（若 Web HMI 不稳定）：** 使用 Figma 原型（Task B2 产出）代替
- 在访谈纪要 YAML 中标注 `stimulus_material: figma_fallback`

## 访谈纪要 YAML 结构（每份 interview-XX.md 的 frontmatter）

```yaml
---
interviewee_id: captain-FCB-01       # 匿名化；格式：captain-{TYPE}-{NN}
vessel_type: FCB                      # FCB | TUG | FERRY
years_experience: ~                   # 整数
current_role: Master                  # Master | Chief_Officer | OOW | ROC_Operator
interview_date: 2026-06-XX
interview_duration_min: ~
stimulus_material: web_hmi            # web_hmi | figma_fallback
scan_priority: []                     # T1/T2 产出：[CPA_TCPA, AIS_targets, ...]
sat2_comprehension_score: ~/5         # think-aloud 评分，见 transparency-questionnaire.md
transparency_preference: ""           # T4 直接引用受访者原话（中文）
tor_experience: ""                    # T3 直接引用受访者原话（中文）
sign_off: false                       # 签字认可此纪要：true | false
fallback_source: false                # 是否为 fallback ③ 资料（非实时访谈）
annotations: {}
---
```

## 参考文献

- [R4] Veitch et al. (2024) Ocean Engineering 299:117257 — ROC 接管时窗 60s + 透明度悖论 D 因子
- [R5-aug] USAARL (2026-02) + NTNU (2024) — SAT 层偏好实证
- [R23] Veitch & Alsos (2022) "From captain to button-presser" — NTNU
- [NLM-HF] Wärtsilä IntelliTug captain interview ground truth 🟢 High
