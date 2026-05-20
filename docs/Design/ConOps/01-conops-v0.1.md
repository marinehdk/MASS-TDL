# Concept of Operations (ConOps) — MASS L3 Tactical Decision Layer — STUB

| Field | Value |
|---|---|
| Document ID | MASS-L3-TDL-CONOPS-001 |
| Version | v0.1-stub |
| Date | 2026-05-19 |
| Status | **STUB** — placeholder for D1.8 deliverable |
| Architecture Baseline | v1.1.2 |
| Development Plan | v3.0 |
| CCS i-Ship Requirement | CG-136 (ConOps is mandatory for AIP) |

## Purpose

This Concept of Operations describes the MASS ADAS L3 Tactical Decision Layer
from the perspective of its users (ROC, Captain, Shore Operator), the operational
environment, and the system's intended behaviour across its operational envelope.
It is a mandatory artefact for CCS i-Ship (Nx, Ri/Ai) AIP submission per CG-136.

## Required Sections (to be filled)

- [ ] **§1 Scope & System Overview**
  - [ ] 1.1 System boundaries (L3 in MASS stack)
  - [ ] 1.2 Key operational capabilities
  - [ ] 1.3 Relationship to other MASS layers (L1/L2/L4/L5)
- [ ] **§2 User Roles & Responsibilities**
  - [ ] 2.1 Remote Operator / Commander (ROC) — MASS Code level 3–4 operation
  - [ ] 2.2 Captain (onboard, override authority)
  - [ ] 2.3 Shore Operator (monitoring, limited intervention)
  - [ ] 2.4 Role transitions under MASS Code mode changes
- [ ] **§3 Operational Scenarios**
  - [ ] 3.1 Normal open-water transit
  - [ ] 3.2 COLREGs encounter resolution (head-on, crossing, overtaking)
  - [ ] 3.3 Multi-vessel encounters (3+ vessels)
  - [ ] 3.4 Degraded sensor conditions (fog, radar outage)
  - [ ] 3.5 Out-of-ODD situation (return to safe state)
  - [ ] 3.6 Emergency takeover (TMR ≤ 60s)
- [ ] **§4 Operational Envelope**
  - [ ] 4.1 ODD parameter ranges (visibility, traffic, waterway)
  - [ ] 4.2 Permitted vessel types (FCB baseline → multi-vessel)
  - [ ] 4.3 Environmental limits (wind/wave/current)
  - [ ] 4.4 Transition to DEGRADED / CRITICAL modes
- [ ] **§5 User Interface Concept**
  - [ ] 5.1 SAT-1/2/3 transparency framework
  - [ ] 5.2 Decision information panels (CPA/TCPA/Rule)
  - [ ] 5.3 Alarm and alert philosophy
  - [ ] 5.4 Takeover request (TOR) modalities
- [ ] **§6 Safety Philosophy**
  - [ ] 6.1 Doer-Checker architecture
  - [ ] 6.2 Safe state definition and entry conditions
  - [ ] 6.3 Graceful degradation path
- [ ] **§7 Assumptions & Constraints**
- [ ] **§8 Reference Documents**

## Reference Documents

- Architecture Design v1.1.2: `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md`
- V&V Plan: `docs/Design/V&V_Plan/00-vv-strategy-v0.1.md`
- HMI Design (M8): `docs/Design/Detailed Design/M8/01-detailed-design.md`
- SIL Frontend: `docs/Design/SIL/v1.0-unified/03-sil-frontend-design.md`
- HAZID RUN-001: `docs/Design/HAZID/RUN-001-kickoff.md`
- CCS CG-136 (ConOps)
- IMO MASS Code (MSC 110/111)

## Development Timeline

| Version | Date | Scope | Status |
|---|---|---|---|
| v0.1 stub | 2026-05-19 | Framework + placeholder sections | 🔴 **STUB** |
| v0.2 draft | 2026-07-31 (D2.8) | Full draft for Phase 2 review | 📅 Planned |
| v1.0 final | 2026-08-31 (D3.8) | AIP-ready version | 📅 Planned |
