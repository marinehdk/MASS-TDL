# M7 Safety Supervisor — Effort Split v2.1

| Field | Value |
|---|---|
| Document | M7 work breakdown — core vs SOTIF explicit split |
| Version | v2.1 (MUST-11) |
| Date | 2026-05-08 |
| Status | Draft — M7-hat sign-off pending (5/12 PM) |
| References | v3.0 plan §0.1 +3.0 pw closure (line 47) · user decision §13.2 (2026-05-07) |

---

## §1 Background

The v3.0 plan allocated M7 Safety Supervisor **9 person-weeks** total (v2.0 had 6 pw). The +3 pw increase was resolved in user decision §13.2 (2026-05-07) as a necessary investment to properly cover both IEC 61508 SIL 2 baseline and ISO 21448 SOTIF analysis. This document makes the split explicit.

---

## §2 M7-core (6 person-weeks, 7/20–8/31)

**Delivery**: D3.7 (target: 8/31 DEMO-3)

### §2.1 Deliverables

| # | Deliverable | Effort | Dependency |
|---|---|---|---|
| 2.1.1 | Doer-Checker arbitration logic implementation | 2.0 pw | D2.7 HARA output (SIF list) |
| 2.1.2 | IEC 61508 SIL 2 baseline implementation (M7 safety functions) | 1.5 pw | D2.5 SIL tool chain; D1.5 V&V Plan |
| 2.1.3 | FMEDA table (≥20 failure modes, M7 internal) | 1.0 pw | D2.7 HARA + external FMEDA M1 |
| 2.1.4 | Watchdog + heartbeat client (RFC-007 M7 side) | 0.5 pw | RFC-007 v1.0 (post 5/13 evaluation) |
| 2.1.5 | M7 unit tests (SIL-grade coverage ≥95%) | 1.0 pw | D1.6 scenario schema |

**Total M7-core**: 6.0 pw

### §2.2 Acceptance Criteria

- [ ] M7 arbitration: MRM triggered within 500ms of SIF activation (IEC 61508 timing requirement)
- [ ] M7 FMEDA PFH budget: < 2×10⁻⁷/hr (SIL 2 allocation; combined M7+M1 < 5×10⁻⁷/hr)
- [ ] RFC-007 heartbeat: M7 liveness signal to X-axis Deterministic Checker operational
- [ ] Unit test coverage: ≥95% on SIF-critical paths; ≥80% overall
- [ ] Doer-Checker independence: M7 code has ZERO shared modules with M1–M6 (enforced by `path_s_dry_run` CI job)

---

## §3 M7-sotif (3 person-weeks, 7/13–8/31)

**Delivery**: Part of D3.7 and D3.9 closure

### §3.1 Deliverables

| # | Deliverable | Effort | Dependency |
|---|---|---|---|
| 3.1.1 | ISO 21448 SOTIF mapping: 6 SOTIF categories → M7 monitoring hooks | 1.0 pw | D2.7 HARA; ISO 21448:2022 |
| 3.1.2 | SOTIF triggering condition catalog (M2 perception degradation → M7 response) | 1.0 pw | D1.6 scenario schema; M2 track confidence model |
| 3.1.3 | RFC-007 integration: M7 SOTIF hooks into heartbeat + X-axis liveness token | 0.5 pw | RFC-007 v1.0 |
| 3.1.4 | D2.7 HARA feedback loop: SOTIF findings → HARA residual risk closure | 0.5 pw | D2.7 HARA (concurrent) |

**Total M7-sotif**: 3.0 pw

### §3.2 Acceptance Criteria

- [ ] ≥6 SOTIF categories documented with M7 monitoring response
- [ ] SOTIF triggering condition catalog: ≥15 conditions covering M2 perception failure modes
- [ ] RFC-007 liveness token includes SOTIF health flag (field defined in RFC-007 HeartbeatMsg)
- [ ] HARA residual risk: M7-sotif findings integrated into HARA by D2.7 deadline (7/31)

---

## §4 Timeline (Candidate 8-Week Window)

| Week | M7-core focus | M7-sotif focus |
|---|---|---|
| W1 (7/13) | Arbitration logic skeleton | SOTIF mapping §3.1.1 |
| W2 (7/20) | IEC 61508 baseline + SIF impl | Triggering condition catalog §3.1.2 |
| W3 (7/27) | FMEDA table draft | HARA feedback loop §3.1.4 |
| W4 (8/03) | Watchdog + heartbeat client §2.1.4 | RFC-007 SOTIF integration §3.1.3 |
| W5 (8/10) | Unit tests §2.1.5 | Catalog review + HARA closure |
| W6 (8/17) | SIL coverage verification | SOTIF catalog final |
| W7 (8/24) | Integration: M7-core + M7-sotif combined tests | — |
| W8 (8/31) | DEMO-3 readiness; D3.7 closure review | D3.7 closure review |

---

## §5 v3.0 Plan Annotation

v3.0 plan §0.1 line 47 `+3.0 pw` (previously unlabeled in v2.0) is now explicitly attributed:
- **+3.0 pw = M7-sotif (3 pw) + reclassification of M7-core from 6→6 pw** (no change to M7-core; the +3 is net new SOTIF work)

v2.1 acknowledge (2026-05-12): this document is the formal record of the split. Plan mathematics remain unchanged (87.0 pw total work / 84.0 pw capacity / -3.0 gap in §0.3 closure path).

---

## §6 M7-hat Sign-off

*[Pending 5/12 PM — M7-hat independent review session]*

Review criteria:
- [ ] M7-core deliverables are executable in 6 pw within the 7/20–8/31 window
- [ ] M7-sotif deliverables are executable in 3 pw with concurrent HARA dependency
- [ ] Timeline (§4) is achievable given V&V engineer availability (5/8–8/31+2w)
- [ ] Acceptance criteria are verifiable and testable

Sign-off: ⚪ Pending

---

*v2.1 — 2026-05-08 — MUST-11 product · References: user decision §13.2 (2026-05-07)*
