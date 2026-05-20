# SIL Simulator Qualification Report — STUB

| Field | Value |
|---|---|
| Document ID | MASS-L3-TDL-SIL-QUAL-001 |
| Version | v0.1-stub |
| Date | 2026-05-19 |
| Status | **STUB** — placeholder for D1.3.1 deliverable |
| Architecture Baseline | v1.1.2 |
| Development Plan | v3.0 |

## Purpose

This document records the qualification evidence for the SIL simulation environment,
demonstrating that the simulation fidelity is sufficient for DEMO-1/DEMO-2/DEMO-3
decision-level validation per DNV-RP-0513 and DNV-RP-0671.

## Required Sections (to be filled)

- [ ] **§1 Executive Summary** — qualification verdict per environment dimension
- [ ] **§2 Simulator Taxonomy & Scope** — what is being qualified (numerical kernel,
      environmental disturbance, sensor mock, vessel dynamics)
- [ ] **§3 Verification Against Reference Data**
  - [ ] 3.1 Straight-line advance (MMG vs FCB trial data)
  - [ ] 3.2 Turning circle (port/starboard)
  - [ ] 3.3 Zig-zag (10°/10°, 20°/20°)
  - [ ] 3.4 Crash-stop
  - [ ] 3.5 Environmental disturbance injection (wind/wave/current)
- [ ] **§4 Sensor Mock Fidelity**
  - [ ] 4.1 Radar noise characteristics
  - [ ] 4.2 AIS message timing
  - [ ] 4.3 GNSS drift profile
- [ ] **§5 Timing Accuracy**
  - [ ] 5.1 Real-time factor (RTF) measurement
  - [ ] 5.2 Latency budget compliance (§6 of V&V Plan)
- [ ] **§6 Limitations & Waivers**
- [ ] **§7 Re-qualification Triggers**

## Reference Documents

- V&V Plan: `docs/Design/V&V_Plan/00-vv-strategy-v0.1.md`
- SIL Architecture: `docs/Design/SIL/v1.0-unified/01-sil-architecture.md`
- FCB Trial Data: TBD (HAZID RUN-001)
- DNV-RP-0513: Simulation Platform Qualification
- DNV-RP-0671: Maritime Simulator Certification

## Qualification Status

| Dimension | Status | Target Date | Notes |
|---|---|---|---|
| Straight-line advance | ❌ Not started | DEMO-1 (6/15) | Requires FCB trial data |
| Turning circle | ❌ Not started | DEMO-1 (6/15) | Requires FCB trial data |
| Zig-zag maneuver | ❌ Not started | DEMO-2 (7/31) | |
| Environmental disturbance | ❌ Not started | DEMO-2 (7/31) | |
| Sensor mock | ❌ Not started | DEMO-2 (7/31) | |
| Timing / RTF | ⚪ Partial | DEMO-1 (6/15) | Preliminary RTF measured; formal report pending |
