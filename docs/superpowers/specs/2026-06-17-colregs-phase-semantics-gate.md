# COLREGs Phase-Semantics Gate Spec (Supplement)

**日期**：2026-06-17
**状态**：Draft（spec + impl 并行）
**关系**：补充 `2026-06-17-colregs-avoidance-fsm-design.md`——该 Spec 改决策逻辑（FSM），本 Spec 改验证 gate（让 FSM 的正确性可被 8-probe 捕获）
**法规源**：IMO COLREGs Consolidated Edition 2018（A 级，原文已提取于 `/tmp/colregs2018.txt`）

## 问题

当前 8-probe gate 用几何 KPI（min CPA、heading_change、final XTE）评估，**不含 COLREGs 阶段语义**。后果：本船"机械向右规避再回航线"能过 gate，即便违反：
- Rule 8(d) effectiveness checked until **finally past and clear**
- Rule 14(a) pass on the **port side**
- Rule 15 **avoid crossing ahead**

`rule_compliance_evaluator.py` 只看 `heading_change_deg` + `cpa_nm`，无阶段判定。根因：trace 不记录 target 几何，gate 无法计算相对态势。

## C1-C7 检查（基于 COLREG 2018 原文条款）

### C1 — Rule 8(d) finally past and clear（核心 bug 修复门）
**条款**：「Action taken to avoid collision... shall be such as to result in passing at a safe distance. The effectiveness of the action shall be carefully checked until the other vessel is **finally past and clear**.」
**检查**：回航线（conflict_detected 由 true→false，或 behavior 回 TRANSIT）时刻：
- target 相对 release-reference-heading 的 bearing > 112.5°（已过正横）AND
- range opening（当前 range < release 时刻 range） AND
- CPA ≥ cpa_safe

> **ERRATA 2026-06-18：** 上述 112.5° abaft-beam 阈值**仅适用于追越（Rule 13）**。Rule 13(b)/21(c) sternlight 135° arc 定义追越扇区为 [112.5°, 247.5°]。对**横越/对头让路（Rule 14/15）**，112.5° 几何不可达——浅角度慢速横越（rule15-cs cog=290/10.6kn）右转避让后目标方位渐近 port beam 前方，永不进 abaft sector。修正：横越/对头用 **90° beam**（过正横）+ **tcpa<0**（已过 CPA）+ range≥cpa_safe opening 三项 AND。内部设计报告 §4.2 自己就写 `abaft_threshold = 112.5 if is_overtaking else 90.0`——原 spec 与内部报告自相矛盾，本 errata 对齐。
> 详见 `docs/superpowers/specs/2026-06-18-colregs-c1-crossing-beam-fix.md`。
> **已知局限**：即使 90° beam，rule15-cs 在实际 runtime（避让仅 55s）目标 rel_brg 仍永不超过 59°——根因不只是 C1 阈值，还有避让时长不足 + M6 过早 release。90° beam 修复对 rule15-cs-edge（29kn，rel_brg=121°）有效，对 rule15-cs（10.6kn）需配合避让架构修复。

**失败表现**：目标船尾未过正横即回航线（你观察到的 bug）
**实现**：gate 后处理，用 trace 的 target+ownship 轨迹

### C2 — Rule 8(b) readily apparent, no succession of small alterations
**条款**：「alteration... shall be large enough to be readily apparent... a succession of small alterations... should be avoided.」
**检查**：避让窗口（conflict_detected=true 期间）内：
- heading 单调段累计变化 ≥ 30°（一次大幅，非多次小幅）AND
- 无连续 ≥3 次 < 10° 抖动（heading_change 符号反转 < 10°）
**实现**：ownship heading 时间序列（trace 已有）

### C3 — Rule 8(a)/16 ample time, early and substantial
**条款**：「positive, made in **ample time**」（8a）；「take **early** and substantial action」（16）
**检查**：首次避让 onset（heading 开始偏离航线 > 5°）时 TCPA ≤ T_plan（ODD-A=720s，C-12 A 级）AND onset 不是紧急（TCPA > T_emergency）
**实现**：tcpa 时间序列（需 trace 记录 ownship 的 tcpa，或从 target+ownship 反算）

### C4 — Rule 14(a) pass on the port side
**条款**：「each shall alter her course to starboard so that each shall pass on the **port side** of the other」
**检查**：Rule14 对头场景，避让结束后本船从目标的**左舷**通过——即 CPA 时刻目标相对本船 bearing 在 port 侧（0°-180° 范围内偏 port），等价：避让期间 lateral_offset（本船相对原航线的横向位移）方向与目标 bearing 方向一致，使本船让到目标左舷。
**实现**：target + ownship 位置，CPA 时刻判定

### C5 — Rule 15 avoid crossing ahead
**条款**：「the vessel which has the other on her own starboard side shall keep out of the way and shall... **avoid crossing ahead** of the other vessel」
**检查**：Rule15 交叉给路场景，本船从目标**船尾**通过——CPA 时刻本船在目标沿航向轴的**后方**（along-axis < 0）。
**实现**：target cog + 位置，沿轴投影

### C6 — Rule 17 stand-on 三阶段
**条款**：17(a)(i) keep course and speed；17(a)(ii) may take action when give-way not acting；17(b) must act when collision unavoidable
**检查**：stand-on 角色（Rule17-cr-so 场景）：
- stage 1/2（TCPA > T_act）：heading_change < 5°（保向）
- stage 3（TCPA ≤ T_act）：允许动作，且按 17(c) 不向 port 侧左转（对 port 侧目标）
**实现**：timing_stage（需 trace 记录或从 tcpa 反算）+ heading

### C7 — Rule 13(d) overtaking finally past and clear
**条款**：「Any subsequent alteration of the bearing... shall not... relieve her of the duty of keeping clear of the overtaken vessel until she is **finally past and clear**.」
**检查**：追越场景，release 时本船在目标沿航向轴**前方**（along-axis > 0 + margin）AND range opening。
**实现**：target cog + 位置，沿轴投影

## Trace 数据格式升级

当前 trace 缺 target 几何。新增 `/sil/target_vessel_state` 记录（每 target 每周期）：
```json
{"sim_t": 123.4, "topic": "/sil/target_vessel_state",
 "target_id": 1, "lat": 30.5, "lon": 122.3,
 "cog": 180.0, "sog_kn": 12.0, "heading_deg": 180.0}
```
+ ownship record 增补 `tcpa_min_s` 字段（本船对最近目标的 TCPA）。

**采集点**：`docker/sil_topic_bridge.py` DebugTraceWriter 已有 topic 白名单，加入 target_vessel_state。

## Gate 集成

`run_6_scenarios.py` 的 `compute_overall_pass` 追加：
```python
phase_semantics_ok = c1_past_clear and c2_apparent and c3_ample and (
    (rule == 14 and c4_port_side) or
    (rule == 15 and c5_not_cross_ahead) or True)  # rule-specific
overall_pass = ... and phase_semantics_ok
```
`rule_compliance_evaluator.py` 每条 rule handler 重写，调用 C1-C7 检查。

## 验证

VERIFY-RED：当前代码 + 升级 gate → 8-probe 应红（机械右转违反 C1/C4）。
VERIFY-GREEN：FSM 修复（RELEASE 要求 past_clear）+ 升级 gate → 8-probe 应绿。

## 来源
- IMO COLREGs Consolidated Edition 2018（Rule 8/13/14/15/16/17/19 原文）A 级
- 现状代码：`rule_compliance_evaluator.py`、`run_6_scenarios.py:588-700`、`sil_topic_bridge.py`
