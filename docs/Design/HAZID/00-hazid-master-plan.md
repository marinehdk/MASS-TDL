# FCB 初始 HAZID 工作主计划

> **目的**：基于 v1.1.1 架构 + 附录 E HAZID 校准任务清单，组织 FCB 实船试航前的初始 HAZID（Hazard Identification）工作组活动。
>
> **架构基线**：v1.1.1（`docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告_v1.1.1.md` 附录 E）
>
> **创建时间**：2026-05-06
> **预计执行**：12–14 周（与软件详细设计并行）

---

## 1. 工作目标

### 1.1 HAZID 范围

针对 v1.1.1 中所有标注 `[初始设计值]` / `[TBD-HAZID]` / `[HAZID 校准]` 的参数，通过：
1. **风险识别**：每个参数的失效场景 + 影响等级
2. **校准方法**：FCB 实船试航 + 水池试验 + 蒙特卡洛 + 实证文献
3. **校准产出**：参数最终值 + 不确定区间 + 适用 ODD 子域 + 适用船型

### 1.2 5 大类参数（合计 25+ 项）

| 类别 | 参数数量 | 文件 | 优先级 |
|---|---|---|---|
| **E.1 ODD 框架参数** | 5 类 / 12 项 | `01-odd-parameters.md` | 高 |
| **E.2 M7 MRM 命令集参数** | 4 类 / 6 项 | `02-mrm-parameters.md` | **极高**（SOTIF 关键）|
| **E.3 M7 SOTIF 监控阈值** | 4 项 | `03-sotif-thresholds.md` | 高 |
| **E.4 M5 双层 MPC 参数** | 3 项 | `04-mpc-parameters.md` | 高 |
| **E.5 M6 COLREGs 参数** | 2 项 | `05-colregs-parameters.md` | 高 |

---

## 2. HAZID 工作组组织

### 2.1 参与方

| 角色 | 团队 | 职责 |
|---|---|---|
| **HAZID 主持人** | 项目经理 / 系统集成总师 | 流程协调 + 时间表 |
| **L3 系统架构师** | L3 战术层 | 参数初始值 + 工程依据解释 |
| **操纵性专家** | 水动力 / FCB 船型 | MMG 4-DOF 模型 + 实船操纵性数据 |
| **CCS 验船师** | 外部 | i-Ship 入级合规性审查 |
| **DNV 验证官** | 外部（独立第三方）| 形式化验证 + 独立挑战 |
| **HF 工程师** | 内部 | TMR 60s + 操作员实验 |
| **COLREGs 法规专家** | 内部 / 海事法规 | Rule 8/13–17 合规验证 |
| **风险评估师** | 安全工程 | LOPA / FMEA / FTA |

### 2.2 工作模式

```
迭代式 HAZID：3 轮 × 4 周 = 12 周（基础范围）+ 多船型扩展 4 周
   │
   ├── 轮 1（周 1–4）：FCB 初始 HAZID
   │   ├── 周 1：参数清单 review + 失效场景识别
   │   ├── 周 2：实证数据采集（水池 / 仿真）
   │   ├── 周 3：风险评估 + 阈值优化
   │   └── 周 4：校准结果归档 + 回填 v1.1.1
   │
   ├── 轮 2（周 5–8）：FCB 实船试航数据采集
   │   ├── ≥ 100 小时实船运营数据
   │   └── ML 阈值优化（极大似然法 / 贝叶斯回归）
   │
   ├── 轮 3（周 9–12）：HAZID 校准结果固化
   │   ├── 阈值最终值 + 不确定区间
   │   ├── ODD 子域适配性验证
   │   └── CCS / DNV 评审
   │
   └── 多船型扩展（周 13–14+）：拖船 / 渡船参数适配（不在初始 HAZID 范围内）
```

---

## 3. HAZID 通用模板

每个参数的 HAZID 校准文档**必须**包含：

```yaml
parameter:
  id: <唯一编号>
  name: <参数名>
  module: <模块名>
  section: <v1.1.1 §x.y>
  initial_value: <v1.1.1 中的初始值>

risk_assessment:
  failure_scenarios:
    - scenario_id: ...
      description: ...
      severity: catastrophic | major | minor
      likelihood: frequent | probable | occasional | remote | improbable
      RPN: severity × likelihood
  affected_modules: [list]
  affected_ODD_subzones: [ODD-A/B/C/D]

calibration_method:
  primary: <方法 1>
  secondary: <方法 2>
  data_source: <实船 / 水池 / 仿真 / 文献>
  acceptance_criteria: <校准通过的 KPI>

calibrated_value:
  point_estimate: <最终值>
  uncertainty_interval: [<lower>, <upper>]
  applicability:
    ODD_subzones: [list]
    vessel_type: [FCB / Tugboat / Ferry / ...]
  HAZID_session_date: <YYYY-MM-DD>
  HAZID_attendees: [list]

backfill_strategy:
  v1.1.1_target_section: <§x.y>
  backfill_format: <"[初始设计值 X，HAZID YYYY-MM 校准为 Y]" / 全替换 / 添加注脚>
  approver: <架构评审委员会 / CCS / DNV>
```

---

## 4. HAZID 风险评估等级（参考 IEC 61508 + LOPA）

### 4.1 严重性（Severity）

| 等级 | 定义 | 示例 |
|---|---|---|
| **Catastrophic** | 碰撞 + 人员伤亡 + 船舶损失 | MRM-01 减速时长不足 → 与目标船相撞 |
| **Major** | 单船触底 + 重要损失 | ODD 阈值过松 → 进入禁航区 |
| **Minor** | 任务中断 + 经济损失 | TDL 系数不准 → ROC 频繁接管 |

### 4.2 可能性（Likelihood）

| 等级 | 定义 |
|---|---|
| Frequent | > 10⁻²/年 |
| Probable | 10⁻³ – 10⁻²/年 |
| Occasional | 10⁻⁴ – 10⁻³/年 |
| Remote | 10⁻⁵ – 10⁻⁴/年 |
| Improbable | < 10⁻⁵/年 |

### 4.3 风险优先级数（RPN）

```
RPN = Severity × Likelihood
RPN ≥ 12（Catastrophic × Frequent）→ 必须降低，停止 AIP 申请
RPN 8–11 → 须缓解，回填风险登记
RPN ≤ 7 → 可接受
```

---

## 5. 校准产出 + 回填策略

### 5.1 校准结果归档

每次 HAZID 会议后产出：
1. 会议纪要（含参与方 + 决议）
2. 校准结果数据表（参数最终值 + 不确定区间）
3. 风险登记（已评估 + 已缓解 + 残留风险）
4. v1.1.1 → v1.1.2 patch 计划（哪些参数需回填正文）

### 5.2 v1.1.1 → v1.1.2 回填规则

```
回填前（v1.1.1）：
  TDL = min(TCPA_min × 0.6, T_comm_ok, T_sys_health)  [设计值: 0.6 = TCPA 60%, HAZID 校准]

回填后（v1.1.2）：
  TDL = min(TCPA_min × 0.55, T_comm_ok, T_sys_health)  [HAZID 2026-08 校准为 0.55, 不确定区间 ±0.05; FCB 适用]
```

### 5.3 回填触发的 v1.1.2 patch list

每轮 HAZID 后：
1. 收集所有校准结果
2. 编制 v1.1.1 → v1.1.2 patch list
3. 架构评审委员会批准
4. 同步 Capability Manifest 文件（每船型独立更新）

---

## 6. 与其他工作流的关系

| 工作流 | 关系 |
|---|---|
| 详细功能设计（M1/M2/M3/M4/M6/M8）| 详细设计中所有 [HAZID 校准] 参数 → HAZID 工作组提供初始数据 |
| 跨团队对齐（RFC-001~006）| RFC-005 Reflex Arc 触发阈值依赖 HAZID 校准 |
| CCS AIP 申请 | HAZID 报告是 ConOps + ODD-Spec 的核心证据文件（v1.1.1 §14.4）|

---

## 7. 时间表 + 关键里程碑

```
2026-05-06（今天）  →   HAZID 工作组组建
2026-05-13（周 1）   →   参数清单 review + 5 大类详细文件评审
2026-05-20（周 2）   →   水池 / 仿真数据采集启动
2026-05-27（周 3）   →   首轮风险评估
2026-06-03（周 4）   →   首轮校准结果归档
2026-06-10–07-08（周 5–8）→   FCB 实船试航数据采集（100+ 小时）
2026-07-15–08-12（周 9–12）→   阈值最终化 + CCS / DNV 评审
2026-08-19            →   v1.1.1 → v1.1.2 patch 完成
2026-09 月            →   提交 CCS AIP（前置依赖完成）
```

---

## 8. 文件清单

```
docs/Design/HAZID/
├── 00-hazid-master-plan.md（本文件）
├── 01-odd-parameters.md（E.1 ODD 框架参数）
├── 02-mrm-parameters.md（E.2 M7 MRM 命令集参数）
├── 03-sotif-thresholds.md（E.3 M7 SOTIF 监控阈值）
├── 04-mpc-parameters.md（E.4 M5 双层 MPC 参数）
└── 05-colregs-parameters.md（E.5 M6 COLREGs 参数）
```
