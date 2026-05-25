# Certification Evidence Tracking — STUB

| Field | Value |
|---|---|
| Document ID | MASS-L3-TDL-CERT-TRACK-001 |
| Version | v0.1-stub |
| Date | 2026-05-19 |
| Status | **STUB** — placeholder for D1.8 deliverable |
| Architecture Baseline | v1.1.2 |
| Development Plan | v3.0 |
| Certification Target | CCS i-Ship (Nx, Ri/Ai), IEC 61508 SIL 2, ISO 21448 SOTIF |

## Purpose

This document tracks the production status of all certification evidence artefacts
required for CCS i-Ship (Nx, Ri/Ai) AIP submission and IEC 61508 SIL 2 certification.
It serves as the single source of truth for certification readiness across all phases.

## Evidence Repository Structure

```
docs/Design/
├── ConOps/          — Concept of Operations (CCS AIP mandatory)
├── Safety/HARA/     — Hazard Analysis & Risk Assessment
├── Safety/FMEDA/    — Failure Mode Effects & Diagnostic Analysis
├── Safety/ALARP/    — ALARP demonstration (Phase 4)
├── Cert/            — This tracking document + audit trail
├── V&V_Plan/        — V&V strategy + gate criteria
├── SIL/             — Simulation qualification + coverage
└── Architecture Design/ — Architecture evidence (v1.1.2 baseline)
```

## Evidence Status Matrix

| Evidence Artefact | CCS Req | SIL 2 Req | Status | Target Date | Owner |
|---|---|---|---|---|---|
| ConOps v0.1 → v1.0 | ✅ Mandatory | — | 🔴 **STUB** | D1.8 (stub) / D2.8 (draft) / D3.8 (final) | TBD |
| Hazard Log / HARA | ✅ Mandatory | ✅ Mandatory | 🟡 **v0.1 complete — D2.7 2026-07-10** | D3.3 (LOPA) / D3.8 (final) | Safety Engineer |
| FMEDA (M1, M7) | — | ✅ Mandatory | 🟡 **M1 v1.0 complete — D2.7 2026-07-10; M7 → D3.3a** | D3.3a (M7) | Safety Engineer |
| ALARP Demonstration | ✅ Mandatory | ✅ Mandatory | 🔴 **Not started** | Phase 4 | TBD |
| SDLC Plan (IEC 61508-3 §7) | — | ✅ Mandatory | 🔴 **Not started** | Phase 4 | TBD |
| V&V Plan v0.1 → v1.0 | ✅ Mandatory | ✅ Mandatory | 🟡 Draft complete | D1.5 (v0.1) / D3.8 (v1.0) | V&V Engineer |
| SIL Qualification Report | — | ✅ Supporting | 🔴 **Not started** | D1.3.1 | V&V Engineer |
| Architecture Report v1.1.2 | ✅ Mandatory | ✅ Supporting | 🟢 Complete | Already delivered | System Architect |
| Detailed Design (M1–M8) | ✅ Mandatory | ✅ Supporting | 🟢 Complete | Already delivered | System Architect |
| RFC Resolutions | ✅ Supporting | — | 🟢 Complete | Already delivered | Team |
| HAZID RUN-001 | ✅ Mandatory | ✅ Mandatory | 🟡 In progress | 8/19 | HAZID Team |
| Cybersecurity (RFC-007) | ✅ Mandatory | — | 🔴 Not started | D3.9 | Cyber Engineer |
| Source Code (colcon) | — | ✅ Required | 🟡 Skeleton | Ongoing | Dev Team |
| Unit / Integration Tests | ✅ Supporting | ✅ Required | 🟡 In progress | Ongoing | Dev + V&V |
| Static Analysis (MISRA) | ✅ Supporting | ✅ Required | 🟢 Complete (D1.4) | 2026-05-20 | Dev Team |
| SIL 1000 Run Record | ✅ Supporting | ✅ Required | 🔴 Not started | D3.6 | V&V Engineer |
| Training & Competence | ✅ Supporting | — | 🔴 Not started | D2.6 / D3.5' | HF Consultant |

## Key Milestones

| Milestone | Date | Gates |
|---|---|---|
| D1.8 Cert evidence stubs | 6/15 | Phase 1 exit |
| D2.8 Full evidence draft | 7/31 | Phase 2 exit |
| D3.8 AIP-ready evidence | 8/31 | Phase 3 exit |
| CCS AIP Submission | 11/2026 | Phase 4 gate |
| SIL 2 Third-Party Assessment | Q4 2026 | DNV/TÜV/BV |
| ALARP + SDLC complete | v1.1.4 (2027 Q1) | Before cert sea trial |

## D2.7 · HARA v0.1 + FMEDA M1 v1.0（2026-05-22 完成）

| 证据项 | 文件路径 | 状态 | 日期 |
|---|---|---|---|
| HARA v0.1（32 危险源 H-01~H-32）| docs/Design/Safety/HARA/01-hara-v0.1.md | ✅ 完成 | 2026-05-22 |
| FMEDA M1 v1.0（20 失效模式 FM-M1-01~FM-M1-20）| docs/Design/Safety/FMEDA/M1-fmeda-v1.0.md | ✅ 完成 | 2026-05-22 |
| SIF 覆盖验证（SIF-01~SIF-08 全引用）| 内嵌于 HARA 文件 §SIF 覆盖验证表 | ✅ 完成 | 2026-05-22 |
| ODD×Health-State 12 格 pivot 表 | 内嵌于 HARA 文件末尾 | ✅ 完成 | 2026-05-22 |
| Finding C P0-C-1(b) 关闭 | HARA 文件已 commit | ✅ | 2026-05-22 |
| Finding C P1-C-8 关闭 | FMEDA v1.0（20 条）已 commit | ✅ | 2026-05-22 |
| Finding C P0-C-3 部分闭环 | 完整独立性矩阵推 D3.3a | 🟡 部分 | — |

## Reference Documents

- V&V Plan: `docs/Design/V&V_Plan/00-vv-strategy-v0.1.md`
- Architecture: `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md`
- HAZID: `docs/Design/HAZID/RUN-001-kickoff.md`
- Development Plan: `docs/Design/Architecture Design/gantt/MASS_ADAS_L3_8个月完整开发计划.md`
