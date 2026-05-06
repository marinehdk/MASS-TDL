# HAZID — E.2 M7 MRM 命令集参数（v1.1.1 §11.6）

> **类别**：M7 Safety Supervisor MRM（Minimum Risk Maneuver）命令集参数
> **优先级**：**极高**（SOTIF 关键，DNV 验证官重点关注 — F-NEW-001）
> **章节锚点**：v1.1.1 §11.6
> **预计工作量**：3–4 周（FCB 初始 HAZID）

---

## 参数清单

### 1. MRM-01 减速至安全速度（v1.1.1 §11.6）

| ID | parameter | initial_value | severity | calibration_method |
|---|---|---|---|---|
| MRM-001 | MRM-01 速度目标 | **4 kn** | Catastrophic | FCB 18 kn 制动曲线（Yasukawa 2015 4-DOF MMG [R7]）+ COLREG Rule 6 |
| MRM-002 | MRM-01 减速时间 | **≤ 30 s** | Catastrophic | 同上（制动加速度 -0.2 m/s² 验证）|

**v1.1.1 工程依据**：

> COLREG Rule 8 "in ample time" + Rule 6 "safe speed"；目标制动加速度 ≈ -0.2 m/s²（FCB 18 kn 制动距离 600–800 m 的线性化下限）[R7]。FCB 实船试航后用 Yasukawa 2015 4-DOF MMG 拟合的制动曲线校准 [R7]。

**风险评估**：

| 失效场景 | 严重性 | 可能性 | RPN |
|---|---|---|---|
| **4 kn 过低** → FCB 高速制动距离 > 600 m 不可达，与目标船相撞 | **Catastrophic** | Probable | **12（阻塞）** |
| **4 kn 过高** → 残余速度仍可能引发碰撞（特别是密集场景） | Major | Occasional | 6 |
| **30 s 过长** → 紧急避让时间窗压缩 | Major | Probable | 9（须缓解）|
| **30 s 过短** → FCB 制动加速度超物理极限（推力反转 / 螺旋桨损伤）| Major | Remote | 5 |

**校准方法**（按优先级）：

1. **FCB 水池 / 仿真试验**：
   - 用 Yasukawa 2015 4-DOF MMG 模型仿真 18 kn → 4 kn 减速曲线
   - 测量真实制动加速度（含波浪扰动）
   - 校准 30 s 是否充足

2. **FCB 实船试航**：
   - 18 kn 巡航 → 4 kn 减速实测（≥ 5 次 / 海况 ≥ 2 个）
   - 采集真实制动距离 + 加速度
   - 与水池仿真对比，校准模型偏差

3. **COLREG Rule 8 合规性验证**：
   - 减速 30 s 内目标船是否有充分识别时间（"in ample time"）
   - 减速量是否 "large alteration"（速度减半 = 大幅）

4. **多船型适配**：
   - 拖船：低速大推力，减速时间可能更短（待校准）
   - 渡船：尺寸大，减速距离更长（待校准）

**预期校准结果**：

```yaml
MRM-01:
  speed_target_kn:
    FCB: <CALIBRATED 3.5–4.5>  # ± 0.5 不确定区间
    Tugboat: <TBD>
    Ferry: <TBD>
  deceleration_duration_s:
    FCB: <CALIBRATED 25–35>  # ± 5 不确定区间
    Tugboat: <TBD>
    Ferry: <TBD>
  applicable_ODD: [ODD-A, ODD-B, ODD-D]  # ODD-C 港内用 MRM-04
```

---

### 2. MRM-03 紧急转向（v1.1.1 §11.6）

| ID | parameter | initial_value | severity | calibration_method |
|---|---|---|---|---|
| MRM-003 | MRM-03 转向角度 | **±60°** | Major | COLREGs Rule 8 "large alteration" + 目标船识别度 |
| MRM-004 | MRM-03 转艏率 | **0.8 × ROT_max** | Major | FCB 操纵性数据 + 安全裕度 |

**v1.1.1 工程依据**：

> COLREGs Rule 8 "large alteration" + Rule 17 "best aid to avoid collision"；±60° 是保守值（≥ "大幅" 30° 经验值的 2 倍以保证目标船清晰可识别）[R18, R20]

**风险评估**：

| 失效场景 | 严重性 | 可能性 | RPN |
|---|---|---|---|
| ±60° 过小 → 目标船未识别避让动作（违反 Rule 8 "readily apparent"）| Major | Probable | 9（须缓解）|
| ±60° 过大 → FCB 高速大转向引发横倾 / 失稳 | **Catastrophic** | Remote | 9 |
| 0.8 × ROT_max 过快 → 横倾 / 货物移位 | Major | Occasional | 6 |
| 转向方向（左 vs 右）选择错误 → COLREG 违规 | **Catastrophic** | Improbable | 5 |

**校准方法**：

1. **MMG 仿真**：FCB 4-DOF 模型，不同转向角度 + 转艏率组合的操纵稳定性
2. **目标船识别度实证**：HIL 模拟器中，ROC / 船长对不同转向幅度的识别时间
3. **法规复核**：COLREGs Rule 8 "in ample time" + "large alteration" 量化解读
4. **方向选择逻辑**：COLREGs Rule 17 兜底（基于威胁方位决定）

**预期校准结果**：

```yaml
MRM-03:
  turn_angle_deg:
    FCB:
      open_water (ODD-A): ±60°  # 初始值确认
      narrow_channel (ODD-B): ±45°  # 减小避免触岸
  rot_factor:
    FCB: 0.7–0.8 × ROT_max  # 含安全裕度
```

---

### 3. MRM-04 抛锚序列（v1.1.1 §11.6）

| ID | parameter | initial_value | severity | calibration_method |
|---|---|---|---|---|
| MRM-005 | MRM-04 适用水深 | **≤ 30 m** | Major | DNV-CG-0264 §9.8 + 锚位预选算法 |
| MRM-006 | MRM-04 适用船速 | **≤ 4 kn** | Major | 与 MRM-01 减速终点对接 |

**v1.1.1 工程依据**：

> DNV-CG-0264 §9.8 警告与安全设备：地理围栏 / 安全锚地查询；MUNIN MRC 设计先例 [R15]

**风险评估**：

| 失效场景 | 严重性 | 可能性 | RPN |
|---|---|---|---|
| 水深 > 30 m 仍尝试抛锚 → 锚链长度不足 / 锚不咬底 | Major | Occasional | 6 |
| 船速 > 4 kn 抛锚 → 锚链断裂 / 拖锚 | **Catastrophic** | Remote | 9 |
| 锚位选择不合理（航道内 / 障碍附近）→ 阻碍其他船舶 / 触底 | Major | Probable | 9（须缓解）|

**校准方法**：

1. **FCB 锚装备规格**：锚链长度 + 锚重 + 抓底力曲线
2. **港口运营手册**：FCB 主要运营海域的水深分布 + 安全锚地位置
3. **MMG 仿真**：抛锚瞬态分析（船速 → 锚链张力曲线）
4. **MUNIN MRC 经验**：[R15] 中类似场景的工程参数

**预期校准结果**：

```yaml
MRM-04:
  max_water_depth_m:
    FCB: 25–35  # 取决于锚链长度
  max_speed_kn:
    FCB: 3.5–4.5  # 与 MRM-01 终点对接
  anchor_position_selection:
    method: "ENC zone_type 查询 + 距离航道 ≥ 0.5 nm"
```

---

## HAZID 工作清单（MRM 专项 — 极高优先级）

| 周次 | 活动 |
|---|---|
| **周 1** | MRM 参数清单 review + 失效场景识别 + 蒙特卡洛碰撞概率 |
| **周 2** | FCB 4-DOF MMG 模型仿真（MRM-01 减速 + MRM-03 转向）|
| **周 3** | FCB 水池试验（如可用）/ 高保真仿真器验证 |
| **周 4** | MRM HAZID 会议 — 多角色（操纵性专家 + COLREGs + CCS）|
| **周 5** | FCB 实船试航 MRM 验证（≥ 5 次每个 MRM 类型）|
| **周 6** | 校准结果归档 + v1.1.1 §11.6 回填 |

---

## DNV 验证官关注点（F-NEW-001 关闭确认）

DNV 在 v1.1 复审时质疑"MRM-01 4 kn / 30 秒无实证基础"。v1.1.1 已修订为 **[初始设计值]** + 工程依据列。HAZID 工作组须：

1. **采集实证数据**：FCB 真实制动曲线 / 操纵性数据
2. **形式化验证**：MRM 触发逻辑可被 DNV 形式化模型化
3. **回填证据**：HAZID 校准结果 → v1.1.2 §11.6 表格 + 附录 D' 修订记录
4. **可审计性**：所有校准过程在 ASDR 中持久化（HAZID 会议纪要 + 仿真数据 + 实船数据）

---

## 校准产出 → v1.1.1 → v1.1.2 回填示例

```diff
v1.1.1 §11.6:
- | MRM-01 | 减速至安全速度 | 速度目标 = 4 kn [初始]，减速时间 ≤ 30 s [初始]
+ | MRM-01 | 减速至安全速度 | 速度目标 = <X> kn [HAZID 2026-XX 校准]，减速时间 ≤ <Y> s
+ |        |                | * 不确定区间 [<low>, <high>]; FCB 适用; ODD-A/B/D
+ |        |                | * 校准依据：FCB 实船试航 ≥ 5 次 + Yasukawa 2015 4-DOF MMG
```

---

## 参考

- v1.1.1 §11.6 / 附录 D' (F-NEW-001 关闭) / 附录 E.2
- [R7] Yasukawa & Yoshimura 2015 / [R18] COLREGs / [R20] Eriksen 2020
- DNV-CG-0264 §9.7 / §9.8 / [R15] MUNIN MRC
