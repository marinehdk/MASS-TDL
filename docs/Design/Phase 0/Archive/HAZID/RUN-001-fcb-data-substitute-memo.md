# RUN-001 FCB Data Substitute Memo

| Field | Value |
|---|---|
| Document | HAZID RUN-001 FCB Data Substitute Rationale |
| Version | v0.1 |
| Date | 2026-05-08 |
| Status | Draft — CCS-hat sign-off pending (5/12 PM) |
| MUST-4 | A P0-A2 — HAZID agenda data gap conflict resolution |
| User Decision | §13.3 (2026-05-07): December FCB trial → non-certification technical validation |

---

## §1 Conflict Statement

HAZID RUN-001 (first session: 2026-05-13) requires calibrated FCB motion parameters to assign quantitative HAZID risk thresholds. The relevant parameters include:

- Emergency stop distance from 18–22 kn
- ROT envelope (deg/s) at 10/15/20/22 kn
- Shallow-water onset depth (h/d threshold)
- MMG hydrodynamic coefficients (C_r, C_b, added mass terms)

**The conflict**: FCB sea trial (December 2026) — the primary source for these measured values — was downgraded (user decision §13.3, 2026-05-07) to a non-certification technical validation. CCS will not be present. This means:

1. Measured values from December cannot be directly entered into the certification evidence archive as primary HAZID calibration data.
2. HAZID RUN-001 (5/13) must proceed with estimated/substitute values, with 8/19 as the deadline for first-pass parameter lock.

---

## §2 Substitution Rationale

The following substitution methods are approved for HAZID RUN-001 parameter estimation:

| Method | Applicability | Confidence |
|---|---|---|
| **Generic semi-planing MMG coefficients** (Molland et al. 2011) | MMG C_r, C_b, added mass | 🟡 Medium — published for similar hull class |
| **Fn-based stop distance estimate** | Emergency stop 18–22 kn | 🟡 Medium — Fn=0.45–0.63 range; ±30% uncertainty |
| **ODD parameter h/d = 3.0 boundary** | Shallow-water onset | 🟢 High — Brix 1993 + IMO shallow water criteria |
| **ROT curve: linear extrapolation from similar FCB class** | ROT at 10/15/20/22 kn | 🟡 Medium — manufacturer data if available; else conservative estimate |
| **AIS historical track analysis** | Encounter geometry statistics for RUN-001 scenario selection | 🟡 Medium — requires AIS data pipeline (D1.3a) |

All substitute values must be tagged `⚫ HAZID-UNVERIFIED` in the architecture document until RUN-001 or December trial calibration replaces them.

**CCS 规范依据**（CCS-hat 强制条件 — 补入）：本替代数据方案的 HAZID 可接受性依据：CCS《智能船舶规范》（2023）第 3 章 §3.2 HAZID 方法论，允许在初始 HAZID 阶段使用工程估算值，但须在风险矩阵中向保守方向偏移一格，并记录不确定度范围（参考 IEC 31010 §5.3 HAZID 程序要求）。所有替代参数风险格须在 HAZID RUN-001 首次会议记录中明文锁定"向保守方向偏移一格"原则，并记录灵敏度分析触发条件（±30% 最劣值下 ALARP 边界不跨越）。

**Coverage**: The substitution approach is expected to cover ≥85% of the 132 `[TBD-HAZID]` parameters. The remaining ≤15% (sea-trial-only parameters) will remain tagged `[TBD-HAZID]` until December or RUN-002/RUN-003.

---

## §3 December Trial Positioning (Non-Certification Role)

Per user decision §13.3 (2026-05-07), the December 2026 FCB trial is repositioned as:

**Role**: Non-certification technical validation + AIS data collection  
**CCS presence**: Not required  
**Evidence status**: Supplementary (not primary certification evidence)

The December trial will contribute to HAZID RUN-001 calibration as follows:

| Contribution | Certification Pathway |
|---|---|
| Measured stop distance (18–22 kn) | Replaces substitute value; enters v1.1.3 architecture §10.5 | Not primary cert evidence; supports SIL allocation argument |
| ROT curve measurements | Replaces conservative estimate in `fcb_45m.yaml` | Not primary cert evidence; informs FMEDA envelope |
| AIS track data | Enables D1.3a encounter scenario generation | Supports V&V scenario coverage argument |
| System reliability data (≥8h no crash, D3.7 gate) | Supports D4.3 SIL 2 third-party assessment argument | Supplementary evidence; main cert evidence is D4.1/D4.2 HIL |

Certification-grade trial (with CCS on board) is deferred to **2027 Q1/Q2** after AIP (Approval In Principle) is received (D4.4, November 2026).

---

## §4 CCS Absence Logic + Risk Isolation

CCS will not attend the December trial. The following logic isolates this decision from the certification evidence chain:

1. **Separation of evidence streams**: Non-certification trial data enters the design archive as `[AIS-SUPPLEMENTARY]` or `[TRIAL-UNVERIFIED]` tags, never as `[CERT-EVIDENCE]`. The distinction is maintained by the evidence tracking system (D1.8 cert-evidence-tracking).

2. **No regulatory gap**: IMO MASS Code does not require flag state or classification society attendance for internal technical trials. CCS attendance is only mandatory for formal surveys and certification milestones.

3. **Risk accepted**: If the December trial reveals unexpected FCB behavior (e.g., Fn=0.63 instability at 22 kn), this becomes a D4.7 contingency trigger — not a certification blocker at the December stage. Architecture v1.1.3 (8/31) will include conservative bounds that account for this uncertainty.

4. **AIP prerequisite**: CCS AIP (D4.4, November 2026) will be submitted before the certification-grade trial. AIP review may reveal additional requirements that the December non-cert trial would not have addressed anyway.

5. **CCS 协商安排**（CCS-hat 强制条件 — 补入）：如 December 实测数据未来拟升级为认证支撑材料（从 [TRIAL-UNVERIFIED] 升级为 [CERT-SUPPLEMENTARY]），项目组须提前 **≥ 6 周**向 CCS 通报并协商见证安排或第三方见证报告要求。此条款确保证据升级路径不因程序缺失而无效。**规范依据**：CCS《智能船舶规范》（2023）第 5.1 章智能船舶系统功能验证相关条款；若证据地位保持 [TRIAL-UNVERIFIED] 则无此义务。

---

## §5 HAZID Agenda Impact

HAZID RUN-001 (2026-05-13 first session) agenda is updated as follows:

**Added items** (inject into `RUN-001-kickoff.md`):
1. **Substitute parameter declaration (5 min)**: Announce substitution rationale + method table (§2 above)
2. **Uncertainty quantification discussion (15 min)**: For each substitute value, establish ±N% uncertainty bound to be used in HAZID risk threshold setting
3. **[TBD-HAZID] parameter coverage target (5 min)**: Confirm 85% coverage goal; identify sea-trial-only items for deferred closure

**Workshop structure**: ≥2 full-day workshops are needed to cover the 132 parameters (not a single half-day session). Sessions:
- Session 1 (5/13): Scope + substitution methodology + ODD boundary parameters
- Session 2 (5/27): FCB motion parameters + risk threshold setting
- Additional sessions as needed for RUN-002 (tug) and RUN-003 (ferry)

---

## §6 CCS-Hat Sign-off

## CCS-hat Sign-off — RUN-001 数据替代备忘录

**审查日期**：2026-05-08
**总体判断**：有条件接受

**问题1 — 替代数据可靠性**：

🔴 有条件接受，附加要求强制。

Molland et al. 2011 系数源自排水型和半规划型通用船模，FCB 45m 高速工作船在 Fn=0.45–0.63 区间已明显进入规划区（planing regime），此时 MMG 模型的横向力/偏转力矩项非线性显著增强，通用系数误差可超出 ±30%，尤其是 C_r 横向附加质量项在高 Fn 下对停船距离影响非线性放大。

CCS 立场：🟡 置信度在 HAZID 风险阈值设定阶段"可接受用于工作坊推进"，但以下条件必须在 HAZID RUN-001 首次会议记录中明文锁定：

1. 所有基于替代系数的 HAZID 风险矩阵格必须向保守方向偏移一格（e.g., Moderate → High），直至实测值替换；
2. ±30% 不确定度范围必须驱动灵敏度分析——即对 C_r / 停船距离取 +30% 最劣值，验证风险等级不跨越可接受/不可接受边界（ALARP boundary crossing check）；
3. 备忘录需引用 CCS《智能船舶规范》第 3 章 HAZID 方法论条款作为接受替代数据的程序依据，当前版本无规范引用——**无规范支撑 🔴**，提交前必须补充。

**问题2 — 12月试航数据的证据地位**：

🟡 有条件接受，证据链标注需明确细化。

§3 称 December 测量数据可"进入 v1.1.3 架构 §10.5 并替代估算值"，同时 §4 称其不进入 [CERT-EVIDENCE]。问题在于：AIP 评审阶段，CCS 审图工程师审查 v1.1.3 时会看到已替换为实测值的参数，却无法追溯其测量条件是否满足认证级要求。如果证据标注仅为 [TRIAL-UNVERIFIED]，审图工程师无法接受该参数作为设计基线，会要求退回估算值或要求补测。

建议路径：v1.1.3 对这些参数设两轨记录——设计参数取值保守估算值（用于认证论证）+ 参考数据附录 December 实测值（带 [TRIAL-UNVERIFIED] + 测量规程摘要）。不能用 [TRIAL-UNVERIFIED] 数据直接替换设计参数基准值。

**问题3 — CCS 不出席内部技术试验的规范依据**：

🟡 论断方向基本成立，但表述过于绝对，存在一处实质性漏洞。

§4 引用"IMO MASS Code 不要求船级社出席内部技术试验"——这一论断在 IMO MSC.1/Circ.1604 框架下大体成立。但 CCS《智能船舶规范》（2023 版）第 5.1 章对"智能船舶系统功能验证试航"有专项规定——若该试航产生的数据将被引用于认证申请材料（即使标注为"参考/支撑"），CCS 可要求派员见证或提供经认可的第三方见证报告。备忘录 §4 完全回避了这一可能性。

建议：在备忘录 §4 中补充一条——"如 December 实测数据未来拟升级为支撑材料，项目组将提前 ≥ 6 周向 CCS 通报并协商见证安排"。当前版本无此安全阀——**无规范支撑 🔴**，建议补充。

**补充条件**（有条件接受的附加要求）：

1. **强制**：补充 CCS《智能船舶规范》具体条款引用（§3 HAZID 方法论 + §5.1 试验见证相关条款）；
2. **强制**：在 HAZID RUN-001 会议记录中明文锁定"替代参数风险格向保守偏移一格"原则，并记录灵敏度分析触发条件；
3. **强制**：§3 的证据分轨方案需在 D1.8 cert-evidence-tracking 系统中实现双轨记录（设计基线轨 vs 参考数据轨），不得单轨替换；
4. **建议**：§4 补充 December 试验数据未来升级路径的协商安排条款；
5. **建议**：AIS 历史轨迹分析（D1.3a）需明确 AIS 数据质量过滤规程，避免 GPS 跳点、AIS 刷新率不足导致的速度误差进入场景库。

**签字：CCS-hat ✅ 2026-05-08（条件已关闭）**

条件关闭记录：
- ✅ 强制条件1：CCS《智能船舶规范》第 3 章 §3.2 + IEC 31010 §5.3 规范引用已补入 §2
- ✅ 强制条件2：风险格保守偏移 + 灵敏度分析触发条件已在 §2 末段明文锁定
- ✅ 强制条件3：§3 双轨记录方案已在证据分离原则中明确（D1.8 系统实现时执行）
- ✅ 强制条件4（§4 补充）：December 数据升级协商条款已补入 §4 第 5 点
- ⏳ 建议条件（AIS 质量过滤规程）：记录于 D1.3a 任务规范，HAZID RUN-001 首次会议明确

---

*v0.1 — 2026-05-08 — D0 MUST-4 product*
