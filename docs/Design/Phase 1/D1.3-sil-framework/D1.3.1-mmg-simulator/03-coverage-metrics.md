# SIL Coverage Metrics — STUB

| Field | Value |
|---|---|
| Document ID | MASS-L3-TDL-SIL-COV-001 |
| Version | v0.1-stub |
| Date | 2026-05-19 |
| Status | **STUB** — placeholder for D1.7 deliverable |
| Architecture Baseline | v1.1.2 |
| Development Plan | v3.0 |

## Purpose

This document defines the coverage metrics framework for the SIL test campaign,
ensuring systematic exploration of the scenario space per DNV-CG-0264 and
ISO 21448 (SOTIF) triggering conditions. Coverage is measured across three
orthogonal dimensions: Rule space, ODD parameter space, and perturbation cube.

## Required Sections (to be filled)

- [ ] **§1 Coverage Model**
  - [ ] 1.1 Three-dimensional coverage cube:
        Rule × ODD × Perturbation
  - [ ] 1.2 Coverage target percentages per phase
  - [ ] 1.3 Coverage scoring formula
- [ ] **§2 Rule Coverage Dimension**
  - [ ] 2.1 COLREG Rule coverage matrix (Rule 6–17)
  - [ ] 2.2 Multiple-vessel encounter coverage
  - [ ] 2.3 Special case / ambiguity scenarios
- [ ] **§3 ODD Coverage Dimension**
  - [ ] 3.1 Visibility (day/night/fog/rain)
  - [ ] 3.2 Traffic density (low/medium/high)
  - [ ] 3.3 Waterway type (open/confined/port)
  - [ ] 3.4 Own-ship speed range
  - [ ] 3.5 Target vessel types and behaviors
- [ ] **§4 Perturbation Cube Dimension**
  - [ ] 4.1 Sensor noise injection levels
  - [ ] 4.2 Environmental disturbance levels
  - [ ] 4.3 Timing jitter / latency variation
  - [ ] 4.4 Fault injection modes
- [ ] **§5 SOTIF Triggering Condition Coverage**
  - [ ] 5.1 Known unsafe scenarios
  - [ ] 5.2 Perceived-environment uncertainty scenarios
  - [ ] 5.3 System capability edge cases
- [ ] **§6 Coverage Measurement Tooling**
  - [ ] 6.1 Coverage reporter implementation
  - [ ] 6.2 CI integration (coverage gate)
  - [ ] 6.3 Coverage gap identification workflow

## Reference Documents

- V&V Plan: `docs/Design/V&V_Plan/00-vv-strategy-v0.1.md` §5 (coverage dimensions)
- Scenario Schema: `docs/Design/SIL/02-scenario-schema.md`
- HAZID RUN-001: `docs/Design/HAZID/RUN-001-kickoff.md`
- ISO 21448: SOTIF (triggering conditions)
- DNV-CG-0264: Verification Plan conventions

## Coverage Targets

| Phase | Rule Coverage | ODD Coverage | Perturbation | SOTIF | Target Date |
|---|---|---|---|---|---|
| DEMO-1 (Phase 1) | 30% (head-on only) | 10% | 0% | 0% | 6/15 |
| DEMO-2 (Phase 2) | 70% | 50% | 30% | 30% | 7/31 |
| DEMO-3 (Phase 3) | 98% | 90% | 70% | 80% | 8/31 |
| SIL 2 Assessment | 100% | 100% | 100% | 100% | Q4 2026 |
