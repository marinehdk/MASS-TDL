# HAZID — E.5 M6 COLREGs 参数（v1.1.1 §9.x）

> **类别**：M6 COLREGs Reasoner ODD-aware 参数
> **优先级**：高
> **章节锚点**：v1.1.1 §9.3
> **预计工作量**：3 周（与 ODD 阈值 HAZID 协同）

---

## 参数清单

| ID | parameter | initial_value | severity | calibration_method |
|---|---|---|---|---|
| COLREG-001 | T_standOn（直航船获准行动 TCPA）| 8/6/10 min（FCB/拖船/渡船）| Major | Wang 2021 [R17] + 文献范围 + 实船数据 |
| COLREG-002 | T_act（让路船行动 TCPA）| 4/3/5 min（FCB/拖船/渡船）| Major | 同上 |
| COLREG-003 | Rule 8 "大幅" 转向角 | 30°（v1.1.1 §9.2 经验值）| Major | MMG 参数敏感性 + 目标船识别度 |

---

## 风险评估

### COLREG-001 / 002：T_standOn / T_act 阈值

| 失效场景 | 严重性 | 可能性 | RPN |
|---|---|---|---|
| **T_standOn 过短**（直航船过早行动）→ COLREGs Rule 17 违规 | **Catastrophic** | Probable | **12（阻塞）** |
| **T_act 过短**（让路船过晚行动）→ Rule 8 "in ample time" 违规 | Major | Probable | 9（须缓解）|
| **T_act 过长**（让路船过早行动）→ 任务效率下降 | Minor | Frequent | 4（可接受）|
| 跨船型阈值差异不明显 → Capability Manifest 配置混乱 | Minor | Occasional | 3 |

**v1.0 / v1.1.1 现状**：

> R2 调研已发现 v1.0 的具体阈值数字"很可能来自 Wang 2021 原文的特定船型参数或作者自行设定"；文献中 TCPA 阈值范围是 5–20 min（Frontiers 2021，MDPI Ships 2022），与 v1.0 的 12 min/4 min 存在差异。

**校准方法**：

1. **文献回归**：Frontiers 2021 + MDPI Ships 2022 + Wang 2021 [R17] 综合 TCPA 范围
2. **FCB 实船试航**：≥ 50 个 COLREGs 遇障场景（多种 ODD 子域 × 让路 / 直航角色）
3. **ROC 操作员实验**：基于 HIL 模拟器，操作员对不同 TCPA 阈值的接受度
4. **多船型适配**：FCB / 拖船 / 渡船的操纵性差异（拖船更灵活，T_act 短；渡船尺寸大，需更长）

**预期校准结果**：

```yaml
T_standOn:
  FCB:
    ODD-A: 7–9 min  # ± 1 min 不确定
    ODD-B: 5–7 min
  Tugboat:
    ODD-A: 5–7 min
  Ferry:
    ODD-A: 9–11 min

T_act:
  FCB:
    ODD-A: 3–5 min
    ODD-B: 2–4 min
  Tugboat: 2–4 min
  Ferry: 4–6 min
```

---

### COLREG-003：Rule 8 "大幅" 转向角

| 失效场景 | 严重性 | 可能性 | RPN |
|---|---|---|---|
| 30° 过小 → 目标船未识别避让动作（违反 "readily apparent"）| Major | Probable | 9（须缓解）|
| 30° 过大 → FCB 高速大转向引发横倾 / 任务延误 | Major | Occasional | 6 |

**v1.1.1 §9.2**：经验值 30°（MRM-03 紧急避让用 ±60°，是大幅的 2 倍以保证清晰可识别）

**校准方法**：

1. **MMG 仿真**：FCB 4-DOF 模型，30° 转向的轨迹偏移 + 横倾角度
2. **目标船识别度实证**：HIL 模拟器，对面船 / 让路船对不同转向幅度的识别时间
3. **法规细化**：COLREGs Rule 8 "in ample time" + "readily apparent" 量化解读
4. **跨 ODD 子域**：开阔水域 vs 狭水道（30° vs 45°）

**预期校准结果**：

```yaml
rule_8_large_alteration:
  FCB:
    ODD-A: 30° ± 5°  # 开阔水域
    ODD-B: 45° ± 5°  # 狭水道（更明显的避让动作）
  注：MRM-03 用 ±60°，是 Rule 8 大幅的 2 倍兜底
```

---

## HAZID 工作清单

| 周次 | 活动 |
|---|---|
| 周 1 | 文献回归 + Wang 2021 + Frontiers 2021 综合 |
| 周 2 | FCB 实船试航（≥ 50 COLREGs 场景）+ HIL ROC 实验 |
| 周 3 | 多船型适配（拖船 / 渡船参数）+ HAZID 会议归档 |

---

## 与 ODD 阈值 HAZID 协同（E.1）

COLREGs 阈值与 ODD 子域 CPA/TCPA 阈值（E.1 ODD-001~005）**强相关**：

- T_standOn / T_act 必须 < ODD 子域 TCPA 阈值（确保 COLREGs 行动时机在 ODD 余量内）
- Rule 8 大幅角度的转向半径（半径 = 速度² / (g × tan(横倾角))）须 < CPA 阈值
- ODD-D 能见度不良（CPA × 1.5 倍率）下 T_standOn / T_act 是否需同步放大

**协同校准建议**：HAZID 工作组将 ODD（E.1）+ COLREGs（E.5）合并为单一 session，3 周完成。

---

## 校准产出 → v1.1.1 → v1.1.2 回填示例

```diff
v1.1.1 §9.3:
- T_standOn: 8/6/10 min（FCB/拖船/渡船）
- T_act: 4/3/5 min（FCB/拖船/渡船）
+ T_standOn(FCB): 7–9 min [HAZID 2026-XX 校准, ODD-A]
+ T_act(FCB): 3–5 min [HAZID 2026-XX 校准, ODD-A]
+ * 拖船 / 渡船独立校准 — 详见 Capability Manifest
```

---

## 参考

- v1.1.1 §9.3 / 附录 E.5
- [R17] Wang 2021 / [R18] COLREGs 1972 / Frontiers 2021 综述 / MDPI Ships 2022
- HAZID 协同：E.1 ODD 阈值
