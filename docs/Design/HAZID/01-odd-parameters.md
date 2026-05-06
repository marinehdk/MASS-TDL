# HAZID — E.1 ODD 框架参数（v1.1.1 §3.x）

> **类别**：ODD / Operational Envelope 框架参数
> **优先级**：高
> **章节锚点**：v1.1.1 §3.2 / §3.3 / §3.4 / §3.5 / §3.6
> **预计工作量**：4–5 周（FCB 初始 HAZID）

---

## 参数清单

### 1. ODD 子域 CPA / TCPA 阈值（v1.1.1 §3.3）

| ID | parameter | initial_value | severity | calibration_method |
|---|---|---|---|---|
| ODD-001 | ODD-A 开阔水域 CPA | 1.0 nm | Major | Wang 2021 [R17] + 蒙特卡洛 + COLREG Rule 8 合规 |
| ODD-002 | ODD-A 开阔水域 TCPA | 12 min | Major | 同上 |
| ODD-003 | ODD-B 狭水道 CPA | 0.3 nm | Major | TCPA 文献范围 5–20 min（Frontiers 2021）|
| ODD-004 | ODD-B 狭水道 TCPA | 4 min | Major | 同上 + 实船 VTS 区运营数据 |
| ODD-005 | ODD-C 港内 / 靠泊距离阈值 | 米级（待定）| Major | 港口运营手册 + 实船靠泊数据 |
| ODD-006 | ODD-D 能见度不良 倍率 | 1.5× | Major | IMO COLREG Rule 19 + 实船恶劣海况数据 |

**风险评估**：

| 失效场景 | 严重性 | 可能性 | RPN |
|---|---|---|---|
| CPA 阈值过松 → 实际碰撞距离不足 | Catastrophic | Probable | 12（**阻塞**）|
| CPA 阈值过严 → 频繁误避让 / 任务效率下降 | Minor | Frequent | 6（可接受）|
| TCPA 阈值过短 → 避让时机太晚（让路船 Rule 8 违规）| Major | Probable | 9（须缓解）|

**校准方法**：
1. **蒙特卡洛仿真**：1000+ 多船遇障场景，统计 CPA/TCPA 与碰撞概率的关系曲线
2. **FCB 实船试航**：≥ 50 nm 自主航行，采集真实 CPA/TCPA 分布
3. **CCS 验船师确认**：阈值符合 CCS《智能船舶规范》安全裕度要求
4. **多船型适配**：FCB 校准后，单独适配拖船 / 渡船（轮 4）

**校准产出格式**：

```
CPA(ODD_subzone, vessel_type) = <value>  # 不确定区间 [low, high]
TCPA(ODD_subzone, vessel_type) = <value>
```

---

### 2. TDL 公式系数（v1.1.1 §3.4）

| ID | parameter | initial_value | severity | calibration_method |
|---|---|---|---|---|
| ODD-007 | TDL = TCPA_min × **0.6** | 0.6 | Major | Veitch 2024 [R4] TMR ≥ 60s 实证 + FCB 操作员实验 |

**说明**：v1.1.1 §3.4 公式：
```
TDL = min(TCPA_min × 0.6, T_comm_ok, T_sys_health)
```

`0.6 = TCPA 60% 留 40% 余量供操作员接管`，须 HAZID 校准。

**风险评估**：

| 失效场景 | 严重性 | 可能性 | RPN |
|---|---|---|---|
| 0.6 过低 → 余量不足，TDL ≤ TMR 时 ROC 接管时窗压缩 | Major | Occasional | 6 |
| 0.6 过高 → MRC 触发过早，频繁 ROC 介入 | Minor | Probable | 6 |

**校准方法**：
1. **Veitch 2024 复现**：n=32 RCT，可用时间因子（20s vs 60s）的接管成功率拟合
2. **FCB ROC 操作员实验**：≥ 10 名操作员 × 多场景，统计实际接管时间分布
3. **公式回归**：基于实船数据回归"TCPA × k = 实际可用接管时间"的最佳 k 值

**预期校准结果**：k = 0.55–0.65（FCB），± 0.05 不确定区间

---

### 3. EDGE 状态阈值（v1.1.1 §3.5）

| ID | parameter | initial_value | severity | calibration_method |
|---|---|---|---|---|
| ODD-008 | EDGE 边界条件 margin | < 20% | Minor | 安全裕度对标 + ROC 操作员反馈 |

**风险评估**：

| 失效场景 | 严重性 | 可能性 | RPN |
|---|---|---|---|
| 20% 过小 → EDGE 预警太晚，OUT 状态前余量不足 | Major | Occasional | 6 |
| 20% 过大 → 频繁 EDGE 提示，操作员疲劳 | Minor | Frequent | 4 |

**校准方法**：
1. **安全裕度对标**：航空业 STANDS 标准 + 海事 LOPA
2. **ROC 操作员反馈**：HIL 实验中 EDGE 提示频率 vs 实际介入率
3. **统计校准**：EDGE 状态滞留时长应足够 ROC 准备介入（典型 30–60 s）

---

### 4. Conformance_Score 阈值（v1.1.1 §3.6）

| ID | parameter | initial_value | severity | calibration_method |
|---|---|---|---|---|
| ODD-009 | IN 阈值 | > 0.8 | Minor | 实船 ≥ 100 h 数据 + ML 阈值优化 |
| ODD-010 | OUT 阈值 | < 0.5 | Major | 同上 + LOPA |

**校准方法**：
1. **实船数据采集**：FCB ≥ 100 h 运营数据（含 ODD 转移事件）
2. **ML 优化**：基于"实际安全状态评估准确率"最大化，反推阈值
3. **混淆矩阵**：误报率 < 5% / 漏报率 < 1% 的阈值组合

---

### 5. Conformance_Score 三轴权重（v1.1.1 §3.6）

| ID | parameter | initial_value | severity | calibration_method |
|---|---|---|---|---|
| ODD-011 | w_E（环境）| 0.4 | Major | 极大似然法 / 贝叶斯回归 |
| ODD-012 | w_T（任务）| 0.3 | Major | 同上 |
| ODD-013 | w_H（人机）| 0.3 | Major | + Veitch 2024 TMR 实证 [R4] |

**初始权重假设依据**（v1.1.1 §3.6）：
- E 轴权重最高：环境降质（Beaufort 风级 / 能见度 / Hs）直接限制感知与机动能力，不可通过软件干预
- T 轴次之：传感器 / 通信故障可通过冗余切换降级，软件层有缓冲
- H 轴最低：D1–D4 由法规预定义，权重影响在响应时间余量

**风险评估**：

| 失效场景 | 严重性 | 可能性 | RPN |
|---|---|---|---|
| E 权重过低 → 恶劣海况下 Score 仍高，未触发降级 | **Catastrophic** | Remote | 9（须缓解）|
| H 权重过低 → ROC 失联未及时反映在 Score | Major | Occasional | 6 |
| 权重不归一化 → 三轴贡献失衡 | Minor | Improbable | 2（可接受）|

**校准方法**：
1. **极大似然法**：基于 FCB ≥ 100 h 数据 + "已知安全状态" 标签
2. **贝叶斯回归**：先验权重 (0.4, 0.3, 0.3) + 后验更新
3. **多船型敏感性分析**：拖船 / 渡船的 H 轴权重可能不同（D2 备援场景）

---

## HAZID 工作清单

| 周次 | 活动 |
|---|---|
| **周 1** | 参数清单 review + 失效场景识别 + 蒙特卡洛仿真启动 |
| **周 2** | 水池 / 仿真数据采集（CPA/TCPA 阈值校准） |
| **周 3** | TDL 系数 + Conformance Score 阈值 ROC 操作员实验 |
| **周 4** | 首轮 HAZID 会议 — 校准结果归档 |

---

## 校准产出 → v1.1.1 → v1.1.2 回填示例

```diff
v1.1.1 §3.3:
- ODD-A 开阔水域 | CPA ≥ 1.0nm，TCPA ≥ 12min
+ ODD-A 开阔水域 | CPA ≥ <CALIBRATED> nm，TCPA ≥ <CALIBRATED> min
+ * HAZID 2026-XX 校准；FCB 适用；不确定区间 [low, high]
+ * 拖船 / 渡船须独立校准（详见多船型 HAZID 计划）
```

---

## 参考

- v1.1.1 §3.2 / §3.3 / §3.4 / §3.5 / §3.6 / 附录 E.1
- [R4] Veitch 2024 / [R17] Wang 2021 / [R3] MOOS-IvP
- IEC 61508-3:2010 / ISO 21448:2022 / IMO MSC 110 Part 2
