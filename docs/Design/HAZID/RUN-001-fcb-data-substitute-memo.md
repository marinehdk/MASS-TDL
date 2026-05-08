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

*[Pending 5/12 PM — CCS-hat independent review session]*

Review criteria:
- [ ] §1 conflict accurately represents the regulatory situation
- [ ] §2 substitution methods are defensible under CCS《智能船舶规范》HAZID requirements
- [ ] §3 December trial positioning does not create regulatory gap
- [ ] §4 risk isolation logic is coherent
- [ ] §5 HAZID agenda is executable with available resources

Sign-off: ⚪ Pending

---

*v0.1 — 2026-05-08 — D0 MUST-4 product*
