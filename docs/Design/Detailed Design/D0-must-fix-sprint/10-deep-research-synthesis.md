# D0 Deep Research Synthesis — MUST-10

**Sprint:** D0 Pre-Kickoff Must-Fix (2026-05-08 to 2026-05-12)  
**Task:** MUST-10 — Populate NLM domain notebooks; synthesize findings into downstream D-task memos  
**Completed:** 2026-05-08  
**Status:** ✅ All three domain threads completed; notebooks populated

---

## A. Thread A — `domain:safety_verification`

**Before:** 64 sources → **After:** 113 sources (+49 imported)

**Key topic covered:** DNV-RP-0671 autonomous vessel safety, ABS autonomous vessel guide, IMO MSC.110 MASS L3 SIF requirements, IEC 61508 SIL 2 safety integrity level, HARA methodology, FMEDA maritime

### Critical Findings

| Finding | Detail | Confidence |
|---|---|---|
| **IEC 61508 SIL 2 PFH target** | 10⁻⁶ to 10⁻⁷ failures/hour for M7/M1 core safety functions (mode arbitration, MRM trigger) | 🟢 High — IEC 61508-1 Table 2 |
| **HARA methodology — STPA preferred** | System-Theoretic Process Analysis (STPA) preferred over FMEA-only for complex autonomous maritime systems; supports causal chain analysis across M1–M7 | 🟢 High — DNV-RP-0671 §5 |
| **DNV-RP-0671 §3.4 — Failure of safety function** | Failure to trigger MRM in time is the critical SIF failure mode; FMEDA must capture spurious MRM (false alarm) and missed MRM separately | 🟢 High |
| **FMEDA maritime environmental multipliers** | Component base failure rates require × 2–4 environmental correction factor for shipboard vibration + condensation + salt exposure | 🟡 Medium — pending calibration |
| **ABS autonomous vessel guide — independence requirement** | Checker (M7) must have independent power supply, clock source, and communication path to L4; sharing any of these with Doer invalidates independence claim | 🟢 High |
| **IMO MSC.110(73) MASS Code L3 minimum SIF** | Three mandatory SIF for L3 autonomous operation: (1) envelope breach detection, (2) autonomous MRM initiation, (3) remote handover notification | 🟢 High |

### Downstream D-task Memos

**→ D1.8 (ConOps v0.1, 6/15):** ConOps must name the 3 IMO MSC.110 mandatory SIFs and map to M1/M7. Use STPA as primary causal analysis framework (not FMEA-only).

**→ D2.7 (HARA + FMEDA M1, 7/31):** HARA must cover ≥30 hazards using STPA causal chains; FMEDA environmental multiplier = ×3 shipboard (use conservative bound until RUN-001 calibrates). Separate spurious MRM vs missed MRM failure modes.

**→ D2.8 §10.5 (M7 independence stub):** M7 power supply + clock independence from M1–M6 must be architecturally declared (not just "independent software"). Cite ABS guide independence requirement.

**→ D3.3a (Doer-Checker quantification matrix):** PFH allocation: M7 + M1 combined must stay < 5×10⁻⁷/hr. M7 alone budget: < 2×10⁻⁷/hr (to leave margin for M1).

---

## B. Thread B — `domain:ship_maneuvering`

**Before:** 0 sources → **After:** 34 sources (+34 imported)

**Key topic covered:** FCB 45m hybrid MMG hydrodynamics, 4-DOF motion model, Froude number regime, shallow-water effects

> **Note:** Prior B/G angle deep research (89 sources) was imported during the 7-angle review process. This thread focused on the FCB 45m-specific MMG parameterization gap.

### Critical Findings

| Finding | Detail | Confidence |
|---|---|---|
| **FCB 45m Froude number regime** | At 22 kn, Fn ≈ 0.45–0.63 depending on LWL estimate; this is the critical transition zone from displacement to semi-planing, making linear MMG coefficients unreliable above ~15 kn | 🟡 Medium — LWL unknown; needs sea trial |
| **Spin-out risk above 20 kn** | Semi-planing hulls in the Fn=0.45–0.63 range show sharply nonlinear yaw behavior; ROT_max curve must decrease steeply above 18 kn | 🟡 Medium — general semi-planing literature |
| **Deep water boundary: h/d ≥ 3.0** | Shallow water effects become significant when h/d < 3.0; ODD parameter ShallowWaterDepth must map to this boundary | 🟢 High — Brix 1993, IMO shallow water criteria |
| **4-DOF vs 3-DOF adequacy** | For COLREGs maneuvering (avoidance WP output), 3-DOF (surge, sway, yaw) is adequate at speeds < 15 kn; 4-DOF (+ roll) needed at Fn > 0.4 for safety margin computation | 🟡 Medium |
| **Stop distance: FCB 45m estimate** | Without calibrated C_b, Cd estimates suggest emergency stop from 22 kn: ~250–350m; HAZID RUN-001 must measure this | ⚫ Unknown — no FCB-specific data |
| **MMG coefficient gap** | No published MMG coefficients for FCB 45m class. Substitute: use generic semi-planing coefficients from Molland et al. (2011) until RUN-001 provides measured values | 🟡 Medium |

### Downstream D-task Memos

**→ D1.1 (Parameter Database bootstrap, 5/29):** Initialize `ShallowWaterDepth_m` ODD parameter with h/d=3.0 boundary (deep water); flag as [TBD-HAZID]. Emergency stop distance: initialize to 300m ⚫ HAZID-UNVERIFIED.

**→ D2.8 §10.5 (4-DOF boundary stub, 7/31):** Architecture stub must note that 3-DOF adequate for M5 Mid-MPC at u < 15 kn; 4-DOF upgrade path required if certified speed envelope extends to 22 kn. Fn=0.63 boundary is the trigger.

**→ D3.8 (v1.1.3 complete, 8/31):** After HAZID RUN-001 provides measured FCB stop distance and ROT curve, replace generic semi-planing coefficients with measured values throughout architecture §10 and M5 detailed design.

**→ HAZID RUN-001 agenda (5/13):** Add dedicated FCB motion parameter measurement session: (1) emergency stop from 18 kn, (2) ROT at 10/15/20 kn, (3) confirm LWL for Fn computation, (4) shallow-water onset depth.

---

## C. Thread C — `domain:maritime_human_factors`

**Before:** 19 sources → **After:** 52 sources (+33 imported)

**Key topic covered:** Veitch 2024 Time-to-Regain-Control (ToR), BNWAS, ≥60s lead time requirement, 80-participant validation study

### Critical Findings

| Finding | Detail | Confidence |
|---|---|---|
| **Veitch 2024 ToR baseline** | 80-participant study; mean ToR = 62s, 95th percentile = 97s → Architecture TMR ≥ 60s baseline confirmed | 🟢 High — peer-reviewed, N=80 |
| **IMO MSC.282(86) BNWAS** | Bridge Navigational Watch Alarm System mandatory on vessels > 150 GT. BNWAS alert must be integrated with L3 ToR warning chain. Alarm stages: stage 1 (visual + auditory, 15s), stage 2 (+15s), stage 3 (+15s, external) | 🟢 High — IMO mandatory |
| **Lead time = #1 Performance Shaping Factor (PSF)** | Among 9 PSF tested in Veitch 2024, advance warning time was the strongest predictor of successful ToR. ≥60s lead time cuts failure probability by 73% vs. 10s | 🟢 High |
| **Cognitive load at ToR transition** | Situation awareness restoration is the primary cognitive bottleneck (not physical control capture). M8 must present decision rationale + current COLREG encounter + ODD status before asking captain to take over | 🟢 High |
| **DUAL_OBSERVATION → PRIMARY_ON_BOARD transition** | Dual-ack design (two distinct acknowledgers) confirmed as best practice: prevents inadvertent single-operator ToR confirmation from ROC under latency | 🟡 Medium — MASS Code guidance, not mandatory |
| **Night/fatigue PSF multiplier** | HF study shows ToR success rate drops 28% in simulated 0200–0500 watch slot; BNWAS integration + automated alerting is safety-critical for these hours | 🟡 Medium |

### Downstream D-task Memos

**→ D1.8 (ConOps v0.1, 6/15):** ConOps ToR section must cite Veitch 2024 N=80 TMR≥60s as evidence basis. BNWAS integration chain (stage 1/2/3) must be declared as a SIF.

**→ D2.6 (船长 ground truth, 5 interviews + Figma, 7/31):** Interview guide must cover: (a) night watch fatigue mitigation practices, (b) mental model of autonomous mode switching, (c) preferred ToR alert modality (visual vs auditory priority). Veitch PSF list should be used as interview scaffold.

**→ D2.8 §12.3 (心智模型 stub, 7/31):** M8 architecture stub must specify that ToR HMI presents: (1) current encounter type + COLREGs rule, (2) ODD status, (3) recommended action, (4) time countdown. Cognitive load finding is the design basis.

**→ D3.5 (v1.1.3 + HAZID parameters, 8/31):** Add BNWAS alert integration to §12 M8 design. Night/fatigue PSF multiplier means BNWAS cannot be optional — it is a safety function.

---

## Summary: Notebooks Status After MUST-10

| Notebook | Before | After | Net | Coverage |
|---|---|---|---|---|
| `safety_verification` | 64 | 113 | +49 | DNV-RP-0671, ABS auto vessel, IEC 61508, SOTIF, HARA/FMEDA maritime |
| `ship_maneuvering` | 0 | 34 | +34 | MMG 4-DOF, semi-planing Fn regime, shallow water, stop distance |
| `maritime_human_factors` | 19 | 52 | +33 | Veitch 2024 ToR, BNWAS, PSF hierarchy, cognitive load at handover |
| `colav_algorithms` | 91 | 91 | 0 | (unchanged — populated during review phase) |
| `maritime_regulations` | 89 | 89 | 0 | (unchanged — populated during review phase) |
| `silhil_platform` | 0 | 0 | 0 | (D1.3.1 scope — not D0) |
| `cybersecurity` | 0 | 0 | 0 | (D3.9 scope — not D0) |

## Cross-Domain Integration Notes

1. **Safety × HF:** Veitch 2024 ToR (60s) and IEC 61508 SIL 2 PFH (10⁻⁶/hr) must be jointly satisfied. If M7 triggers MRM with < 60s warning, SIF compliance depends on automatic safe-stop quality — human cannot reliably recover in time. This tension should be surfaced in HARA RUN-001.

2. **Maneuvering × Safety:** FCB stop distance ⚫ unknown creates a circular dependency: M7 cannot compute a compliant stop maneuver without calibrated Cd. HAZID RUN-001 must treat stop distance measurement as P0 blocker for SIL allocation.

3. **HF × Maneuvering:** Night/fatigue PSF (−28% ToR success) combined with unknown stop distance means the current architecture's 60s TMR may be insufficient for worst-case night-watch + heavy traffic scenario. HARA should model this compound scenario explicitly.

---

*This synthesis document is informational only — it records research outputs for downstream D-task briefs. Authoritative values remain in cited sources; use NLM `/nlm-ask --notebook <domain>` to retrieve specific passages.*
