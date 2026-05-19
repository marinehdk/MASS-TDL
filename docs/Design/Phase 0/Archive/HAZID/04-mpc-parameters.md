# HAZID — E.4 M5 双层 MPC 参数（v1.1.1 §10.x）

> **类别**：M5 Tactical Planner 双层 MPC（Mid-MPC + BC-MPC）参数
> **优先级**：高
> **章节锚点**：v1.1.1 §10.3 + §10.4
> **预计工作量**：3 周（与 RFC-001 M5↔L4 对齐协同）

---

## 参数清单

| ID | parameter | initial_value | severity | calibration_method |
|---|---|---|---|---|
| MPC-001 | Mid-MPC 时域 | N=12 步 × 5 s = 60 s（可扩展 90–120 s）| Major | FCB 制动距离约束 + 水池 / 实船试验 |
| MPC-002 | ROT_max（FCB 18 kn）| 12°/s | Major | Yasukawa 2015 [R7] + 实船标定 |
| MPC-003 | BC-MPC 候选航向数 | k=7（±30° 范围）| Minor | 蒙特卡洛 + 鲁棒性测试 |

---

## 风险评估

| 失效场景 | 严重性 | 可能性 | RPN |
|---|---|---|---|
| **Mid-MPC 时域 < 制动距离** → 规划无法兜住高速场景 | **Catastrophic** | Probable | **12（阻塞）** |
| Mid-MPC 时域过长 → 计算开销过大，1-2 Hz 不达 | Minor | Occasional | 3 |
| ROT_max 过保守 → 紧急转向不及时 | Major | Probable | 9（须缓解）|
| ROT_max 过激进 → FCB 横倾 / 失稳 | **Catastrophic** | Remote | 9 |
| BC-MPC 候选数 < 5 → 鲁棒性不足 | Major | Occasional | 6 |
| BC-MPC 候选数 > 10 → 计算延迟 > 100 ms | Minor | Probable | 6 |

---

## 详细校准方法

### MPC-001：Mid-MPC 时域

**v1.1.1 §10.3 初始值**：N=12 步 × 5 s = **60 s**（可扩展 90–120 s）

**校准方法**：
1. **FCB 制动距离反推**：18 kn 制动距离 600–800 m → 时域应 > 制动距离 / 速度 = 65–90 s
2. **COLREGs 合规验证**：Rule 8 "in ample time" 要求充分提前规划
3. **MPC 求解时延**：N=12 步 → 求解 < 500 ms（实时性约束）
4. **多船密集场景**：3+ 船同时遇障，时域是否充分

**预期校准结果**：
```yaml
mid_mpc_horizon:
  FCB:
    base: 90 s  # 兼顾制动 + COLREGs 合规
    extended: 120 s  # 高速 / 密集场景
  Tugboat: 60 s（低速，制动短）
  Ferry: 120 s（大型，制动长）
```

---

### MPC-002：ROT_max（最大转艏率）

**v1.1.1 §10.3 初始值**：12°/s（FCB 18 kn 时实测值）

**校准方法**：
1. **Yasukawa 2015 4-DOF MMG 仿真**：不同速度下的 ROT 极限（速度依赖）
2. **FCB 实船试航**：18 kn / 12 kn / 6 kn 三档速度下的最大安全 ROT
3. **横倾约束**：避免 FCB 在高速 + 大转艏时横倾角 > 安全阈值（典型 25°）
4. **Capability Manifest 写入**：ROT_max 是船型特定参数

**预期校准结果**：
```yaml
ROT_max(speed_kn):
  FCB:
    18 kn: 10–14 °/s  # 不确定区间
    12 kn: 16–20 °/s
    6 kn: 25–30 °/s
  注：速度依赖曲线由 MMG 模型 + 实船数据拟合
```

---

### MPC-003：BC-MPC 候选航向数

**v1.1.1 §10.4 初始值**：k=7（默认 ±30° 范围，δψ = 10°）

**校准方法**：
1. **蒙特卡洛鲁棒性测试**：1000+ 多船遇障场景，统计 k=5/7/9 的最坏情况 CPA 差异
2. **计算延迟测试**：k=7 → BC-MPC 求解 < 100 ms（10 Hz 上限）
3. **目标不确定性敏感性**：θ_uncertainty 范围内 k 是否需自适应
4. **极端场景**：高密度（5+ 船）下 k 是否需扩展至 9–11

**预期校准结果**：
```yaml
bc_mpc_candidates:
  k_default: 7  # ±30° 范围
  k_adaptive_high_density: 9  # 5+ 船密集场景
  delta_psi_deg: 10  # 候选间隔
```

---

## HAZID 工作清单

| 周次 | 活动 |
|---|---|
| 周 1 | Mid-MPC 时域计算开销测试 + 制动距离反推 |
| 周 2 | ROT_max 速度依赖曲线（MMG + 实船） |
| 周 3 | BC-MPC 候选数蒙特卡洛 + HIL 测试 |

---

## 与 RFC-001 协同

MPC 参数校准 与 **RFC-001 M5→L4 接口对齐**强相关：

- AvoidancePlan 频率（1–2 Hz）依赖 Mid-MPC 求解时延（< 500 ms 推 1 Hz；< 250 ms 推 2 Hz）
- ReactiveOverrideCmd 频率（≤ 10 Hz）依赖 BC-MPC 求解时延（< 100 ms）
- HAZID 校准 ROT_max 须同步到 Capability Manifest（L4 LOS+WOP 转弯过渡参数依赖）

---

## 校准产出 → v1.1.1 → v1.1.2 回填示例

```diff
v1.1.1 §10.3:
- N = 12（预测步数，步长 5s，总时域 60s）
- ROT_max = 12°/s（FCB 18 kn时实测值）
+ N = <CALIBRATED 18>  # 总时域 90 s（HAZID 2026-XX 校准）
+ ROT_max(18 kn) = <CALIBRATED 11–13>°/s  # 速度依赖曲线见 Capability Manifest
```

---

## 参考

- v1.1.1 §10.3 / §10.4 / 附录 E.4
- [R7] Yasukawa 2015 MMG / [R20] Eriksen 2020 BC-MPC
- RFC-001 M5↔L4 接口对齐
