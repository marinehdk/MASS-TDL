# MASS ADAS L3 Tactical Decision Layer — Verification & Validation Plan v0.1

| Field | Value |
|---|---|
| Document ID | MASS-L3-TDL-VVP-001 |
| Version | v0.1 |
| Date | 2026-05-31 |
| Author | V&V Engineer (FTE) |
| Owner | V&V Engineer |
| Reviewer | Safety Engineer, DNV Validation Lead |
| Status | Draft for D1.5 Review |
| Architecture Baseline | v1.1.2 |
| Development Plan | v3.0 |
| Certification Target | CCS i-Ship (Nx, Ri/Ai), IEC 61508 SIL 2, ISO 21448 SOTIF |
| Reference Standards | DNV-CG-0264 Section 3, DNV-RP-0513, DNV-RP-0671, IEC 61508-3 Section 7, ISO 21448 |

---

## Revision History

| Version | Date | Author | Description |
|---|---|---|---|
| v0.1 | 2026-05-31 | V&V Engineer | Initial release. Phase 1–4 gate structure, KPI matrix, coverage dimensions, SIL latency budget, RL rebound path, DNV toolchain entry conditions, certification evidence framework, DEMO-1 scope. |

---

## Table of Contents

1. [Purpose & Scope](#1-purpose--scope)
2. [V&V Strategy Overview](#2-vv-strategy-overview)
3. [Entry/Exit Gates](#3-entryexit-gates)
4. [End-to-End KPI Matrix](#4-end-to-end-kpi-matrix)
5. [Test Coverage Dimensions](#5-test-coverage-dimensions)
6. [SIL Latency Budget](#6-sil-latency-budget)
7. [RL Artefact Rebound Path](#7-rl-artefact-rebound-path)
8. [DNV Toolchain Entry Conditions](#8-dnv-toolchain-entry-conditions)
9. [Certification Evidence Tracking](#9-certification-evidence-tracking)
10. [DEMO-1 V&V Scope](#10-demo-1-vv-scope)

---

## 1. Purpose & Scope

This Verification & Validation Plan defines the strategy, criteria, and infrastructure for demonstrating that the MASS ADAS L3 Tactical Decision Layer meets its safety, functional, and performance requirements across the full development lifecycle from first integration through sea trial.

**Scope boundaries:**

- **In scope:** All eight TDL modules (M1 ODD/Envelope Manager through M8 HMI/Transparency Bridge), the SIL/HIL test infrastructure, the scenario generation and coverage tracking framework, the certification evidence pipeline, and the end-to-end performance KPIs.
- **Out of scope:** L1 Mission Layer, L2 Voyage Planner, L4 Guidance Layer, L5 Control & Allocation, Multimodal Fusion, Cybersecurity hardening, and shore-side infrastructure. These layers are treated as test fixtures or boundary conditions.

This plan covers the period from Phase 1 (DEMO-1 Skeleton Live, 15 June 2026) through Phase 4 (HIL integration and third-party SIL 2 assessment, September–November 2026). It is structured in accordance with DNV-CG-0264 Section 3 (Verification Plan) conventions.

---

## 2. V&V Strategy Overview

### 2.1 Three-Phase Verification Approach

Verification proceeds through three strictly sequential phases. Each phase has formal entry and exit gates defined in Section 3. No phase may begin until the preceding phase's exit gate has been cleared.

| Phase | Primary Tool | Time Scale | Focus | Key Deliverable |
|---|---|---|---|---|
| **1. SIL** | Software-in-the-loop simulation | 0.02–1 s timestep | Functional correctness, COLREGs compliance, coverage, performance KPIs | SIL exit report with scenario pass rates, coverage cube, KPI validation |
| **2. HIL** | Hardware-in-the-loop with real-time target | Real-time (at least 10 Hz) | Real-time determinism, DDS latency, hardware driver integration, M7 watchdog on target | HIL exit report with jitter measurements, determinism evidence |
| **3. Sea Trial** | FCB non-certification sea trial | Full mission duration | End-to-end behavior in real environmental conditions, ROC HMI validation, ASDR logging integrity | Sea trial report with encounter logs, KPI replay, anomaly registry |

### 2.2 SIL as Primary Verification Tool

SIL remains the primary verification vehicle for Phases 1 through 3. The majority of functional and safety verification is performed in SIL because scenario space is enumerable and reproducible, fault injection is deterministic, coverage metrics can be collected automatically, and COLREGs compliance can be evaluated against geometric benchmarks with ground-truth geometry.

HIL and sea trial serve as confirmation activities. They do not discover new functional defects; they verify that the real-time implementation, hardware integration, and environmental noise do not degrade the behaviors already proven in SIL.

### 2.3 Independence of V&V

The V&V function is organizationally independent from the development team:

- The V&V Engineer (FTE, 5/8–8/31) owns this plan, the scenario schema, the coverage methodology, and the gate criteria.
- V&V writes test scenarios, operates the SIL framework, and adjudicates gate pass or fail.
- Developers do not modify gate criteria, coverage thresholds, or scenario definitions without V&V review.
- Safety Engineer (outsourced, 5/15–7/10) reviews V&V results for SOTIF and IEC 61508 alignment but does not execute tests.

---

## 3. Entry/Exit Gates

### 3.1 Phase 1 — SIL Entry Gate (E1)

The SIL entry gate ensures that the development baseline is stable enough to yield meaningful verification results.

| ID | Criterion | Threshold | Evidence Artifact |
|---|---|---|---|
| E1.1 | All D1.x tasks closed per v3.0 DoD | 100% closed | D1.x task tracker export |
| E1.2 | colcon build clean on CI | 0 errors, 0 warnings treated as errors | CI build log |
| E1.3 | CI pipeline green | All jobs pass | GitLab pipeline status |
| E1.4 | Scenario schema validated | Schema v1.0 passes structural validation | schema_validate.py report |
| E1.5 | Mock publisher frequencies verified | All cross-layer mock pubs within plus or minus 5% of specified rate | Frequency check log |
| E1.6 | E2E data flow sanity | M1 to M2 to M4 to M5 to M8 message chain completes in under 5 s | E2E latency trace |
| E1.7 | V&V Plan v0.1 committed | File present in docs/Design/V&V_Plan/ | Git commit hash |
| E1.8 | M7 watchdog Python stub at least 1 PASS | At least 1 pytest test with m7 in nodeid passes | pytest-report.json |

Gate E1.1 through E1.8 are checked by `tools/check_entry_gate.py --phase 1`. Manual checks (e.g., design review sign-off) are recorded in the Phase 1 gate log but do not block automated execution.

### 3.2 Phase 1 — SIL Exit Gate (X1)

| ID | Criterion | Threshold | Evidence Artifact |
|---|---|---|---|
| X1.1 | Baseline scenario pass rate | At least 95% of 50 baseline scenarios pass | test-results/baseline_results.json |
| X1.2 | KPI matrix validated | All Section 4 KPIs within threshold on at least 30 scenario runs | test-results/kpi_matrix.json |
| X1.3 | ASDR log consistency | Every decision cycle logged with timestamp, module state, rationale | ASDR audit script report |
| X1.4 | Coverage cube minimum | At least 10 of 1100 cells lit (Phase 1 baseline) | test-results/coverage_cube.json |
| X1.5 | Imazu-22 benchmark | 22 of 22 pass, CPA at least 200 m ratio at least 95%, COLREG classification at least 95% | test-results/imazu22_results.json |
| X1.6 | M7 watchdog critical-path coverage | All 7 monitored modules exercised for timeout and recovery | pytest-report.json |

### 3.3 Phase 2 — HIL Entry Gate (E2)

| ID | Criterion | Threshold | Evidence Artifact |
|---|---|---|---|
| E2.1 | SIL exit gate cleared | X1.1 through X1.6 all pass | Phase 1 exit report |
| E2.2 | HIL hardware qualified | Target hardware flashed and booting | HIL qualification checklist |
| E2.3 | Real-time constraints verified | ROS2 timer jitter P95 under 5 ms on target | ros2 topic hz plus trace analysis |
| E2.4 | DDS-Security profile loaded | Authentication and encryption active on test domain | dds_security.xml validation |

### 3.4 Phase 2 — HIL Exit Gate (X2)

| ID | Criterion | Threshold | Evidence Artifact |
|---|---|---|---|
| X2.1 | Deterministic replay | Same scenario input produces same output within 1% on HIL versus SIL | Replay diff report |
| X2.2 | Latency budget held | dds-fmu bridge P95 under 10 ms, P99 under 15 ms | Latency histogram CSV |
| X2.3 | M7 watchdog on target | Watchdog triggers MRC within 3 timeout periods on hardware | Target test log |
| X2.4 | 200 scenarios run | At least 200 scenarios executed on HIL with at least 95% pass | hil_results.json |

### 3.5 Phase 3 — Sea Trial Entry Gate (E3)

| ID | Criterion | Threshold | Evidence Artifact |
|---|---|---|---|
| E3.1 | HIL exit gate cleared | X2.1 through X2.4 all pass | Phase 2 exit report |
| E3.2 | CCS preliminary approval | CCS surveyor sign-off on ConOps and V&V Plan | CCS letter |
| E3.3 | Crew training complete | ROC and master completed simulator familiarization | Training completion certificates |
| E3.4 | Emergency stop chain tested | Hardware override plus ASDR zero-software e-stop verified on dock | Dockside test log |

### 3.6 Phase 3 — Sea Trial Exit Gate (X3)

| ID | Criterion | Threshold | Evidence Artifact |
|---|---|---|---|
| X3.1 | Mission duration | At least 50 nm and at least 100 h autonomous operation | VDR log plus AIS track |
| X3.2 | Encounter events | At least 10 live COLREGs encounters with target vessels | ASDR encounter registry |
| X3.3 | No safety incidents | Zero MRC activations attributable to TDL defect | Incident registry (empty) |
| X3.4 | KPI replay | All Section 4 KPIs within 2 times threshold when replayed from sea trial logs | Post-processed replay report |

---

## 4. End-to-End KPI Matrix

The following KPIs are measured end-to-end from the first L2 waypoint input to the L4 command output. Measurements are taken under nominal conditions (open sea, Beaufort 0–1, full sensor availability) unless otherwise noted.

| KPI | Target | Measurement Method | Phase Verified |
|---|---|---|---|
| AvoidancePlan P95 latency | Less than or equal to 1.0 s | colcon test timing wrapper on M4 to M5 to M8 chain | SIL Phase 1 |
| ReactiveOverrideCmd P95 | Less than or equal to 200 ms | colcon test timing wrapper on M7 override path | SIL Phase 1 |
| Mid-MPC solve time | Under 500 ms | M5 solver diagnostics (OSQP / ECOS callback) | SIL Phase 2 |
| BC-MPC solve time | Under 150 ms | M5 solver diagnostics (BC horizon) | SIL Phase 2 |
| M7 safety check | Under 10 ms | M7 standalone benchmark (Python stub to C++ target) | SIL Phase 1 |
| M4 arbitration cycle | Under 100 ms | M4 IvP diagnostics (objective function evaluation plus winner selection) | SIL Phase 2 |

**Measurement protocol:**

1. Each KPI is sampled over 1000 consecutive decision cycles.
2. P95 and P99 are computed from the empirical CDF.
3. Outliers beyond P99 are logged as anomalies and reviewed by V&V.
4. If any KPI exceeds its target, the responsible module's D-task is reopened for performance regression analysis.

---

## 5. Test Coverage Dimensions

### 5.1 Functional Coverage

| Dimension | Granularity | Phase 1 Target | Phase 2 Target | Phase 3 Target |
|---|---|---|---|---|
| Module core scenarios | 8 modules times 5 core scenarios each | 40 scenarios | 40 scenarios | 40 scenarios |
| ODD zone variants | 4 zones (open sea, coastal TSS, port approach, offshore wind farm) | 2 zones | 4 zones | 4 zones |
| Functional total | 8 times 5 times zones | 80 runs | 160 runs | 160 runs |

### 5.2 Performance Coverage

- Latency KPIs (Section 4) measured under varying load: 1, 5, 10, 20 concurrent targets.
- Memory footprint: RSS monitored every 10 s during 1 h continuous run.
- CPU utilization: per-core load on target hardware (HIL) or host (SIL).

### 5.3 Failure Response Coverage

M7 Safety Supervisor enforces 6 hard constraints. Each constraint is tested against 10 failure scenarios:

| Hard Constraint | Failure Scenarios (examples) |
|---|---|
| HC-1: CPA at least min safe distance | Sensor dropout, target ghost, speed estimate error |
| HC-2: No ODD violation | ENC mismatch, GPS spoofing, weather threshold breach |
| HC-3: MRC trigger within TMR | Comms loss to shore, M1 deadlock, DDS partition fault |
| HC-4: COLREGs rule never broken | Rule ambiguity, multi-target conflict, crossing plus overtaking |
| HC-5: Speed limit respected | Engine fault, current overspeed, waypoint overshoot |
| HC-6: M8 transparency never silent | M8 crash, logger disk full, DDS topic overflow |

Total failure response test cases: 6 times 10 = 60 cases.

### 5.4 COLREGs Compliance Coverage

The COLREGs compliance matrix is defined as:

- 11 rules (Rule 5, 6, 7, 8, 9, 13, 14, 15, 16, 17, 19)
- 4 ODD zones
- 5 disturbance levels (Beaufort 0–1, 2–3, 4–5, 6–7, sensor degraded)
- 5 PRNG seeds per cell

Total cells: 11 times 4 times 5 times 5 = **1100 cells**.

Phase 1 lights at least 10 cells (baseline). Phase 2 lights at least 200 cells. Phase 3 lights at least 1000 cells (full matrix minus contingency cells reserved for edge cases).

### 5.5 Scenario Distribution Summary

| Phase | Scenario Count | Source | Coverage Focus |
|---|---|---|---|
| Phase 1 (DEMO-1) | 50 | Imazu-22 plus synthetic baseline | Infrastructure validation, schema maturity |
| Phase 2 (DEMO-2) | 200 | Expanded Imazu variants plus COLREGs matrix | Decision quality, M4/M5 maturity |
| Phase 3 (DEMO-3) | 1000+ | Full 1100-cell matrix plus 100+ stress cases | SIL 2 evidence, SOTIF completeness |
| Phase 4 (HIL) | 200 replayed from SIL | Deterministic subset of Phase 3 | Real-time confirmation |
| Phase 4 (Sea Trial) | 10+ live encounters | Unscripted maritime traffic | Environmental validation |

---

## 6. SIL Latency Budget

The SIL toolchain introduces synthetic latency that must be bounded and compensated so that SIL results remain valid predictors of HIL and sea trial behavior.

### 6.1 dds-fmu Bridge Latency

| Metric | Budget | Measurement Point |
|---|---|---|
| P95 latency | Under or equal to 10 ms | ROS2 topic timestamp to FMU input port ready |
| P99 latency | Under or equal to 15 ms | Same as above |
| Max jitter | Under or equal to 5 ms | Absolute deviation from mean per 1 s window |

If the dds-fmu bridge exceeds P99 15 ms, the V&V Engineer shall:

1. Increase the DDS QoS reliability buffer depth.
2. Enable jitter buffering in the FMI co-simulation master.
3. If steps 1 and 2 do not restore the budget, escalate to the Safety Engineer for a latency budget revision.

### 6.2 M7 Native ROS2 Boundary

M7 Safety Supervisor is strictly ROS2 native. It does **not** cross the FMI boundary. This eliminates dds-fmu latency from the safety-critical path and guarantees that M7's VETO decision is always based on the freshest possible world model.

### 6.3 Jitter Buffer Compensation

The SIL framework implements a jitter buffer on each FMU input port:

- Buffer depth: 3 samples (configurable per scenario).
- Timeout: 2 times mean measured jitter; if exceeded, the simulation step is marked DEGRADED and logged.
- Scenarios with more than 1% DEGRADED steps are excluded from pass-rate statistics until the latency root cause is resolved.

---

## 7. RL Artefact Rebound Path

Phase 4 includes reinforcement learning adversarial testing (D4.6, October–December 2026). To prevent a toolchain redesign in Phase 4, the RL artefact rebound path is specified now and locked for the project duration.

### 7.1 Pipeline

```
RL training (Stable Baselines3 / Ray RLlib)
    ↓ export
ONNX model (policy network)
    ↓ convert
mlfmu wrapper (ONNX Runtime inside FMU 2.0)
    ↓ package
FMU 2.0 co-simulation unit
    ↓ reinject
libcosim SIL/HIL co-simulation master
    ↓ observe
TDL module under test (M4 or M5)
```

### 7.2 Interface Contract

- The ONNX model exposes exactly one input tensor (observation vector) and one output tensor (action vector).
- The mlfmu wrapper maps the action vector to a ROS2 message via the existing l3_msgs IDL (v1.1.2 Section 15.1).
- The FMU step size is 100 ms (10 Hz), matching the M4 arbitration cycle.
- The RL policy does not train during SIL/HIL execution. It is a frozen inference graph.

### 7.3 Version Lock

| Component | Locked Version | License | Rationale |
|---|---|---|---|
| ONNX | 1.16+ | Apache-2.0 | Standard export format from PyTorch and TensorFlow |
| ONNX Runtime | 1.18+ | MIT | Jetson-compatible inference engine |
| mlfmu | 0.3.x | BSD-3-Clause | DNV-validated ONNX-to-FMU bridge |
| libcosim | 0.10.x | MPL-2.0 | OSP co-simulation master |

No substitution of these components is permitted without V&V review and a regression test of the full rebound pipeline.

---

## 8. DNV Toolchain Entry Conditions

The SIL/HIL toolchain is subject to DNV-RP-0513 (Software in Certification) and DNV-RP-0671 (Model-Based Verification). The following entry conditions apply before the toolchain is accepted for certification evidence generation.

### 8.1 Version Locking

| Tool / Library | Minimum Version | License | Verification Action |
|---|---|---|---|
| maritime-schema | 0.2.x | Apache-2.0 | Schema validation against v1.0 scenario corpus |
| libcosim | 0.10.x | MPL-2.0 | Deterministic replay test (same input produces same output) |
| farn | 0.9.x | MIT | Batch scenario runner regression test |
| ROS2 Jazzy | 2024.04 | Apache-2.0 | Colcon build plus test on Ubuntu 22.04 |
| FMI standard | 2.0 | — | FMU compliance checker (FMIChecker) |

### 8.2 License Verification

All third-party tools used in the certification evidence pipeline carry permissive open-source licenses (Apache-2.0, MIT, MPL-2.0, BSD-3-Clause). No GPL or proprietary closed-source tools are used in the evidence generation path. License compliance is verified by:

1. `tools/ci/check-licenses.py` scans `pyproject.toml`, `package.xml`, and `CMakeLists.txt` for non-permissive licenses.
2. V&V Engineer reviews the scan report before each phase gate.
3. Any new dependency added after Phase 1 must pass the license scan before it is used in CI.

### 8.3 Self-Qualification Evidence (RP-0513 Section 4)

DNV-RP-0513 Section 4 requires the project to collect evidence that the toolchain itself is fit for purpose. The following evidence is maintained:

| Evidence Item | Collector | Storage Location | Review Frequency |
|---|---|---|---|
| Tool version manifest | CI pipeline | test-results/tool_manifest.json | Every build |
| Deterministic replay log | V&V Engineer | test-results/replay_log/ | Every SIL batch |
| FMU compliance report | fmucheck | test-results/fmu_compliance/ | Every FMU release |
| Schema validation report | schema_validate.py | test-results/schema_validation/ | Every scenario commit |
| License scan report | check-licenses.py | test-results/license_scan/ | Every dependency change |

---

## 9. Certification Evidence Tracking

### 9.1 Evidence Categories

The CCS i-Ship (Nx, Ri/Ai) application requires evidence across 9 categories. Each category is mapped to D-tasks in the v3.0 development plan.

| CCS Category | Description | Mapped D-Tasks | Primary Artifact |
|---|---|---|---|
| C1 — Functional Design | Architecture, interfaces, ODD | D0, D1.1–D1.4, D2.1–D2.4 | Architecture report v1.1.2, IDL specs |
| C2 — Software Lifecycle | SDLC, CM, version control | D1.5, D1.8, D3.9 | Git history, CI logs, change records |
| C3 — Verification Plan | V&V strategy, gates, KPIs | D1.5 (this document) | V&V Plan v0.1–v1.0 |
| C4 — SIL Evidence | Scenario pass rates, coverage, KPIs | D1.6, D1.7, D2.5, D3.6 | test-results/ JSON suite |
| C5 — HIL Evidence | Real-time determinism, hardware tests | D4.1, D4.2 | HIL exit report |
| C6 — Safety Analysis | HARA, FMEDA, SOTIF | D2.7, D3.3a, D3.3b | HARA v0.1, FMEDA tables |
| C7 — Human Factors | ToR, HMI, ROC training | D2.6, D3.5', D4.5' | HF reports, Figma, training records |
| C8 — Sea Trial | Non-certification trial evidence | D4.5 | Trial report, AIS/VDR logs |
| C9 — Cybersecurity | IACS UR E26/E27, DDS-Security | D3.9 | RFC-007, security audit |

### 9.2 Traceability

Every test scenario carries a `cert_evidence_tags` field (array of strings) that links the scenario to one or more CCS categories. The batch runner aggregates these tags into the coverage cube and the final exit report. This ensures that every passing scenario is automatically traced to the certification evidence framework without manual bookkeeping.

### 9.3 Evidence Maturity

| Phase | Evidence Maturity | Action if Gaps Found |
|---|---|---|
| Phase 1 | C1 through C3 complete, C4 stub (50 scenarios) | Reopen D1.6/D1.7 if coverage under 10% of target |
| Phase 2 | C4 expanded (200 scenarios), C6 stub (HARA v0.1) | Escalate to Safety Engineer if HARA blocks FMEDA |
| Phase 3 | C4 complete (1000+ scenarios), C6 complete (FMEDA), C7 complete | Independent DNV review if pass rate under 95% |
| Phase 4 | C5, C8, C9 complete | CCS preliminary review before sea trial |

---

## 10. DEMO-1 V&V Scope

DEMO-1 (15 June 2026) is an internal skeleton-live demonstration. Its V&V scope is strictly limited to **infrastructure verification**. Decision-quality verification is explicitly out of scope for DEMO-1 and is the focus of DEMO-2 (31 July 2026).

### 10.1 In Scope for DEMO-1

| Item | Verification Activity | Success Criterion |
|---|---|---|
| Build system | colcon build on CI and local dev | Zero errors, under 10 min wall time |
| CI pipeline | All stage-1 and stage-2 jobs green | 100% pass rate |
| Scenario schema | Schema v1.0 validates 100% of committed scenarios | schema_validate.py exits 0 |
| Mock publishers | All cross-layer mock nodes publish at specified frequency | Plus or minus 5% rate tolerance |
| E2E data flow | M1 to M2 to M4 to M5 to M8 message chain completes end-to-end | Message received at M8 within 5 s of M1 trigger |
| Gate automation | check_entry_gate.py --phase 1 exits 0 | All automated gates pass |
| Watchdog stub | M7 Python watchdog passes white-box tests | At least 1 test with m7 in nodeid passes |
| Imazu-22 baseline | 22 benchmark scenarios run and aggregate | CPA at least 200 m ratio at least 95%, COLREG classification at least 95% |

### 10.2 Out of Scope for DEMO-1

The following are verified in later phases and are **not** required for DEMO-1 gate clearance:

- M3 Mission Manager route optimization quality.
- M4 Behavior Arbiter IvP multi-objective trade-off correctness.
- M5 Tactical Planner MPC trajectory optimality.
- M6 COLREGs Reasoner rule interpretation accuracy beyond geometric classification.
- M7 Safety Supervisor hard-constraint enforcement under fault injection.
- M8 HMI/Transparency Bridge SAT-1/2/3 compliance.
- Real-time determinism (HIL scope).
- Environmental robustness (sea trial scope).

### 10.3 DEMO-1 Gate Checklist

```
□ E1.1  D1.x tasks 100% closed
□ E1.2  colcon build clean
□ E1.3  CI pipeline green
□ E1.4  Scenario schema validated
□ E1.5  Mock publisher frequencies verified
□ E1.6  E2E data flow under 5 s
□ E1.7  V&V Plan v0.1 committed
□ E1.8  M7 watchdog at least 1 PASS
□ X1.1  50 baseline scenarios at least 95% pass
□ X1.4  Coverage cube at least 10 cells
□ X1.5  Imazu-22 22/22, CPA at least 95%, COLREG at least 95%
```

Run the automated checks:

```bash
python tools/check_entry_gate.py --phase 1 --artifacts-dir test-results/
```

If the script prints `Phase 1 gate CLEARED`, DEMO-1 V&V scope is satisfied.

---

## Document Approval

| Role | Name | Signature | Date |
|---|---|---|---|
| V&V Engineer | | | |
| Safety Engineer | | | |
| Project Manager | | | |
| DNV Validation Lead | | | |

---

*End of Document*
