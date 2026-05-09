# SIL Framework Architecture for L3 Tactical Decision Layer (TDL) COLAV — CCS-Targeted Engineering Recommendation

- **Adopt Option D (Hybrid: ROS2-native SIL kernel with an FMI/maritime-schema interface at the boundary).** Run the L3 TDL as native ROS2 Jazzy components (so the production code is exactly what is tested), but expose a stable, schema-conformant boundary using DNV's `maritime-schema` (MIT) for scenarios and FMI 2.0 / OSP `libcosim` (MPL-2.0) for any ship-dynamics or ML co-simulation modules. This gives CCS-grade evidence quality (DNV-RP-0513-aligned model assurance, schema-standardised scenario/result records), keeps RL extensibility clean (ONNX → `mlfmu` → FMU re-import without touching the certified kernel), and avoids the "test-the-wrapper-not-the-product" anti-pattern that pure-Python orchestration patterns (Option C) create.
- **Use the MMG model from `nikpau/mmgdynamics` (Yasukawa & Yoshimura 2015 formulation, with currents/wind/shallow-water extensions) wrapped behind a C++ `ShipMotionSimulator` abstract** with mode-switched 4-DOF (displacement, ≤12 kn) / 6-DOF (planing, >12 kn) plugins. The same plugin object serves all three duties (own-ship SIL, target-ship SIL, RL environment) by being instantiated multiple times in a single co-sim graph; do **not** fork the model.
- **Build the CCS evidence package around a 1,100-cell coverage cube** (11 COLREGs rules × 4 ODD subdomains × 5 disturbance bins × 5 seeds), seeded by Imazu-22 as mandatory regression, expanded by `trafficgen gen-situation` for parametric sweeps, and recorded as maritime-schema `TrafficSituation` + `CagaTimeStep` Apache-Arrow tables. Map artifacts directly to CCS *Rules for Intelligent Ships 2024* (i-Ship N notations) and harmonise with DNV-CG-0264 + DNV-RP-0513 + IACS UR E26/E27 to maximise reuse if you later seek dual-class.

---

## Key Findings

### 1. Architecture decision (Q1)

| Criterion | A — DNV/OSP/FMI + Python MMG | B — Pure ROS2 SIL | C — NTNU colav-simulator pattern | **D — Hybrid (RECOMMENDED)** |
|---|---|---|---|---|
| **CCS evidence quality** | High (FMI + RP-0513 alignment) but tests Python, not your C++ TDL | Medium — tests real code, but no standardised scenario/result format | Medium — tests Python wrapper, scenario YAML good | **High — tests real ROS2/C++ code AND emits schema-conformant artefacts** |
| **RL extensibility** | Good (mlfmu re-import) | Hard — RL training has to import the whole ROS2 graph | Native (Gymnasium wrapper exists) | Good — Gymnasium adapter sits at the FMI boundary, not inside kernel |
| **Inter-layer interface (L1↔L3↔L4)** | Indirect via FMU signals | Native ROS2 topics (best fidelity) | Wraps ROS2 in Python | **ROS2 native inside; FMI only at the dynamics/RL boundary** |
| **Engineering complexity** | High (FMU build pipelines, conan, MSVC) | Low | Medium | Medium-high (justified by reuse) |
| **CI/CD integration** | Mature (ospx + farn) | Native (`colcon test`, launch_test) | Pytest-based | **Both: colcon test for kernel + farn-driven scenario sweeps** |
| **Risk of "wrong thing tested"** | High | Low | High (the gym wrapper is what's exercised) | **Low** |

**Why D wins:** Options A and C couple your certification evidence to a Python orchestration that is *not* the production binary — the CCS surveyor will ask which artefact corresponds to the binary that runs onboard. Option B gives you authentic test coverage but produces evidence in proprietary `rosbag` form with no industry-recognised scenario language; that hurts portability and forces you to self-justify scenario coverage. Option D keeps the L3 nodes (M1–M8) running in their production ROS2 Jazzy form (so MISRA C++:2023 binaries are what's tested), inserts a maritime-schema-compatible scenario loader and result writer at the harness edge, and uses FMI only where co-simulation buys something concrete (ship dynamics, future ML target ships, classification-society interoperability).

### 2. Layer interface architecture in SIL (Q2)

Adopt a **three-tier mock policy**, varied per test campaign by ROS2 launch arguments:

- **L1 (perception) — "smart mock with playback":** A `world_model_mock` ROS2 node publishes `/world_model/tracks` IDL messages. Three modes selected by launch arg `l1_mode := {synthetic | rosbag | ais_replay}`:
  - `synthetic`: tracks computed analytically from the scenario YAML target-ship trajectories with parameterised noise, dropouts, and detection-range cones (covers ODD/sensor disturbance cells of the cube).
  - `rosbag`: replays AIS/RADAR rosbags collected from the FCB or sister vessels (regression realism).
  - `ais_replay`: streams historical AIS (e.g., MarineTraffic, AISHub, or Norwegian Coastal Administration data) into the same IDL track topic via an adapter node.
- **L2 (voyage planner) — YAML stub feed:** Implement L2 as a *thin* `voyage_stub` node that reads the scenario YAML's `ownShip.waypoints` and publishes `/voyage/plan` and `/voyage/active_waypoint`. Real-stub vs full-sim is unnecessary at L3 SIL; L2 only changes mission goals slowly. Save engineering for L1 fidelity instead.
- **L4 / L5 (propulsion + autopilot) — full MMG-in-the-loop, not a command acceptor.** Reason: BC-MPC's COLREGs Rule 8 robustness depends on realistic actuator delay and yaw dynamics. Use a `ship_dynamics` node wrapping `nikpau/mmgdynamics` (or its FMU export) that subscribes to `/cmd/thrust` and `/cmd/rudder` and publishes `/own_ship/state`. A "command acceptor" mode can be a CI smoke test only.
- **AIS feed:** Two paths share the same IDL topic — a `synth_ais_pub` driven by the scenario, and an `ais_replay_pub` driven by historical NMEA. Both serialise into your 25 internal IDL types so the M2 World Model is identical in either mode.

**Recommended ROS2 patterns:** package per layer-mock, `launch_testing` for assertions (the `ROS2 launch_testing` framework supports `Assert`/`Expect` actions and emits xUnit XML for CI), gtest for pure-algorithmic M4/M5/M6/M7 unit tests, gmock-based mock publishers for interface tests (note that `rclcpp::PublisherBase` is not pure-virtual, so wrap it behind your own interface for mockability — see ROS Answers, ali ekber celik, 2021).

### 3. MMG model — concrete code architecture (Q3)

**Library choice: `nikpau/mmgdynamics`** (Python, Yasukawa & Yoshimura 2015 formulation, MIT-licensed per repo metadata; includes currents/wind/shallow-water extensions and pre-calibrated `kvlcc2_l64` etc.). Avoid `MmgTools/mmg` (LGPL — unrelated tetrahedral remeshing project). `ShipMMG` (mitsuyukiLab) is a viable Python alternative based on the same JASNAOE standardisation but is less mature.

**Plugin pattern:**

```cpp
// fcb/sim/ship_motion_simulator.hpp  (MISRA C++:2023)
class ShipMotionSimulator {
public:
  struct State { double u,v,r,x,y,psi; };
  struct Cmd   { double rudder_rad, nps; };
  virtual ~ShipMotionSimulator() = default;
  virtual State step(const Cmd& c, double dt, const Disturbance& d) = 0;
  virtual ModeTag mode() const = 0;   // DISPLACEMENT_4DOF | PLANING_6DOF
};

class FCBPlugin final : public ShipMotionSimulator {
  DisplacementMMG mmg4_;   // wraps mmgdynamics::pstep  (4-DOF: surge,sway,yaw,roll)
  PlaningHull6DOF mmg6_;   // empirical Savitsky/semi-planing extension
  State step(const Cmd& c, double dt, const Disturbance& d) override {
    return (last_speed_kn_ <= 12.0) ? mmg4_.step(c,dt,d) : mmg6_.step(c,dt,d);
  }
};
```

The same `FCBPlugin` instance is invoked three ways:
1. **Own-ship SIL** — instantiated by `ship_dynamics_node` in the SIL launch graph.
2. **Target-ship SIL** — multiple instances inside `target_ship_pool_node`, one per target listed in scenario YAML.
3. **RL Stage 3** — wrapped by a Gymnasium `Env` adapter for the RL trainer; or, after training, the policy is exported to ONNX, packaged via `dnv-opensource/mlfmu` (MIT, FMI 2.0) into an FMU, and re-loaded into the SIL via OSP `libcosim` (MPL-2.0). This is the *certification-preserving* path: the SIL kernel runs only certified C++/FMU artefacts, never Python/PyTorch.

**Hyperparameter discipline:** A single `vessel_params.yaml` (Lpp, B, C_b, D_p, etc., per the `MinimalVessel` schema) drives `mmgdynamics.calibrate()`; the same YAML hash is recorded with every test run for traceability.

### 4. Scenario library architecture (Q4)

**Schema design — extend, don't replace.** Build on `maritime-schema` `TrafficSituation` (Pydantic + JSON Schema, MIT) and add five custom fields under `metadata` (the schema permits additional properties). This keeps round-tripping with DNV's `trafficgen gen-situation` (MIT) trivial and gives you CCS-credible interoperability evidence.

**Concrete scenario YAML (FCB-OSF-CR-PORT-018, full):**

```yaml
# yaml-language-server: $schema=schemas/fcb_traffic_situation.schema.json
title: "Crossing-from-port, FCB own ship, two targets, Beaufort 5, reduced visibility"
description: "Coverage cube cell rule15 × open-sea × disturbance-D3 × seed-2"
startTime: "2026-05-09T08:00:00Z"
metadata:                            # FCB extension fields (project-specific)
  scenario_id: "FCB-OSF-CR-PORT-018"
  hazid_refs: ["HAZ-NAV-014","HAZ-NAV-022"]
  colregs_rules: ["R15","R16","R8"]
  odd_cell:
    domain: "open_sea_offshore_wind_farm"
    daylight: "twilight"
    visibility_nm: 1.5
    sea_state_beaufort: 5
  disturbance:
    wind: {dir_deg: 235, speed_mps: 12.0}
    current: {dir_deg: 90, speed_mps: 0.6}
    sensor:
      ais_dropout_pct: 5
      radar_range_nm: 6.0
      radar_pos_sigma_m: 25
  seed: 2
  expected_outcome:
    cpa_min_m: 500
    tcpa_min_s: 90
    rule15_compliance: required
    rule8_visibility: "early_and_substantial"
    grounding: forbidden
    pass_criteria:
      cpa_min_m_ge: 300
      heading_change_deg_lt: 60
      no_collision: true
ownShip:
  static: {shipType: "Fast Crew Boat", length: 45.0, width: 9.0, mmsi: 999000018}
  initial: {position: {latitude: 53.5000, longitude: 7.5000},
            sog: 18.0, cog: 0.0, heading: 0.0}
  waypoints:
    - {position: {latitude: 53.6500, longitude: 7.5000}}
targetShips:
  - static: {shipType: "Cargo", length: 120.0, width: 18.0, mmsi: 999000201}
    initial: {position: {latitude: 53.5400, longitude: 7.4500},
              sog: 12.0, cog: 90.0, heading: 90.0}
    waypoints: [{position: {latitude: 53.5400, longitude: 7.6000}}]
  - static: {shipType: "Fishing", length: 28.0, width: 7.0, mmsi: 999000202}
    initial: {position: {latitude: 53.5800, longitude: 7.5200},
              sog: 6.0, cog: 200.0, heading: 200.0}
    waypoints: [{position: {latitude: 53.5400, longitude: 7.4900}}]
environment:                          # used by ship_dynamics + l1 mock
  wind: {dir_deg: 235, speed_mps: 12.0}
  current: {dir_deg: 90, speed_mps: 0.6}
  visibility_nm: 1.5
```

**Coverage cube as CI:** Use `dnv-opensource/farn` (MIT) to generate the 1,100-case folder structure parametrically from a `farnDict`, write each case as the YAML above, then run `colcon test` per case using a `launch_test` parametrised by the scenario file. Group cases by hazard-tag and run the Imazu-22 set on every PR (fast-feedback gate); run the full 1,100 nightly. The Imazu-22 reference geometry is well documented in the literature (Cai/Hasegawa, Sawada et al.) and can be re-encoded into the YAML schema; there is no canonical machine-readable distribution, so freeze your encoded Imazu-22 set at v1.0 and hash it into the evidence package.

**Traceability artefact (one CSV row per test run):**

```
scenario_id, hazid_ref, colreg_rule, odd_cell, disturbance_id, seed,
git_commit, vessel_params_hash, scenario_yaml_hash,
cpa_min_m, tcpa_min_s, rule_compliance_score, doer_checker_overrides,
verdict, evidence_artifact_path, ccs_clause_ref
```

Persist as Parquet (Apache Arrow), aligning with `maritime-schema`'s output table convention so DNV/CCS surveyors get a familiar format. Use `trafficgen gen-situation` for parametric coverage of head-on/crossing/overtaking with relative-bearing sweeps; the tool ships baseline cases under MIT.

### 5. RL boundary architecture (Q5)

**Boundary rule:** *No RL artefact (PyTorch model, training loop, Stable-Baselines3 agent) is permitted to import or be imported by any ROS2 node that is part of the L3 certified kernel.* The boundary is enforced at three levels:

1. **Repo-level:** `/src/l3_tdl_kernel/**` (C++/MISRA, ROS2 nodes M1–M8) and `/src/rl_workbench/**` (Python, Gymnasium, SB3) are separate colcon packages; CI lints prevent cross-imports.
2. **Process-level:** RL training runs against a Gymnasium env (`FCBColavEnv`) that wraps `colav-simulator`-style scenario loading + the **same** `mmgdynamics`-based ship dynamics + a Python re-implementation of the M2/M6 interfaces (observation builder, COLREGs feature extractor). Because both environments share the scenario YAML and the MMG plugin, sim-to-sim transfer is by construction.
3. **Artefact-level:** A trained target-ship policy is exported as `policy.onnx` → `mlfmu build --interface-file iface.json --model-file policy.onnx` produces `target_policy.fmu`. The certified SIL imports the FMU via `libcosim` (OSP, MPL-2.0). No Python in the certified loop.

**Observation/action/reward for adversarial COLREGs-aware target ship:**
- Observation (Box): `[rel_x, rel_y, rel_psi, rel_speed, own_cog, own_sog, ts_role∈{stand_on,give_way}, rule_id_one_hot[8], cpa, tcpa]`
- Action (Box): `[delta_course_deg ∈ [-30,30], delta_speed_kn ∈ [-3,3]]` updated at 1 Hz
- Reward: `+α·proximity_to_critical_CPA −β·rule_violation_indicator −γ·grounding_indicator −δ·trajectory_implausibility` — a classic CADRL-style shaping that produces *plausible-but-stressful* adversaries rather than physics-violating ones (rule_violation gates the reward so adversaries do not "win" by cheating).

**Existing tools triage:**
- **Adapt:** `EivMeyer/gym-auv` (LiDAR-style observation, 2D; no license file in the canonical repo — confirm with author and pin a forked SHA before reuse). `colav-simulator` (NTNU-TTO; see license note below) — strong scenario YAML and Gymnasium wrapper, suitable as the workbench backend.
- **Adapt for sim-to-real reference only:** Sim2Sea (CAS, AAMAS 2026, CC-BY 4.0) — useful as a domain-randomisation recipe.
- **Build from scratch:** the COLREGs reasoner observation/feature extractor that is a faithful Python mirror of your M6 logic.

### 6. CCS AiP evidence package (Q6)

The Chinese regulatory anchor is **CCS *Rules for Intelligent Ships* 2024** (issued May 2024 by CCS Beijing) which classifies "intelligent navigation" capability under the **i-Ship (N)** family — N1 = decision-support/assisted navigation; N2 = remote-controlled navigation; higher levels (autonomous in open waters / full voyage) per Article 9.1.2 (China Classification Society, 2024). The 2018 *Guidelines for Autonomous Cargo Ships* remains in force and is referenced for unmanned cargo concepts. Your FCB targeting "supervised autonomy in open offshore-wind transit" maps cleanest to **i-Ship (N2)**-type evidence, with a forward path to higher autonomy.

**Minimum viable AiP evidence checklist:**

| # | Artefact | Maps to |
|---|---|---|
| 1 | ConOps + ODD definition (open-sea offshore wind farm transit, 18-25 kn FCB, daylight Beaufort ≤6, visibility ≥1 NM) | CCS RIS 2024 Ch. on Intelligent Navigation; DNV-CG-0264 §2; AROS scope |
| 2 | HAZID register with ≥1 HAZ-ID per scenario_id; failure modes per M1–M8 | CCS RIS 2024 Risk Assessment; DNV-CG-0264 |
| 3 | Software development plan (MISRA C++:2023, GitLab CI, code review records) | CCS RIS 2024 software requirements; DNV-CP-0507 (referenced from CG-0264) |
| 4 | Doer-Checker architecture description (M4 doer + M7 checker), with formal proof of safety-net coverage | CCS RIS 2024 functional safety; DNV-CG-0264 fail-safe principle |
| 5 | **Scenario coverage cube execution report**: 1,100 cases, pass/fail per cell, including Imazu-22 baseline (mandatory), trafficgen-generated parametric sweeps, AIS-replay regression | CCS RIS 2024 testing; mirrors "Zhifei" precedent (Brinav/Navigation Brilliance reported 3,000+ collision-avoidance simulation tests with a 0.02% error rate per Xinhua, 21 Feb 2025; Brinav holds an MoU and Type-Approval programme with DNV announced 2024) |
| 6 | Simulation model assurance file (MMG calibration vs sea-trial captive turns/zigzag, 4-DOF↔6-DOF mode-switch validation) | DNV-RP-0513 §2 model assurance; CCS will accept RP-0513-style evidence by precedent |
| 7 | KPI tables: CPA_min, TCPA_min, rule-violation count, heading-change excess, grounding events — with self-justified thresholds (the standards do not numerically prescribe most of these) | CCS RIS 2024 performance; literature precedent (e.g., MDPI JMSE 2025 review reports 0.753 NM minimum-passing-distance benchmarks) |
| 8 | Cyber-resilience design + test evidence (segmented zones, signed updates, audit log) | IACS UR E26 (ship), UR E27 (CBS); applies to newbuilds contracted ≥1 July 2024 |
| 9 | HMI/transparency design (M8) with explainability traces from M6 → operator | CCS RIS 2024 HMI; DNV-CG-0264 MASS-A operator concept |
| 10 | Configuration management & traceability (each test result links scenario_id ↔ git SHA ↔ vessel_params hash ↔ HAZID ↔ rule clause) | CCS general classification practice; ISO/IEC/IEEE 12207 |

**Self-justified vs. standard-prescribed thresholds:** Be explicit — neither CCS RIS 2024 nor DNV-CG-0264 numerically prescribes a CPA threshold; you must self-justify (literature anchor: 300-500 m for FCB-class vessels in open sea is defensible from operator practice and the 0.753 NM ≈ 1,400 m used in some Chinese-academic baselines as a "safe and conservative" target). Document the justification chain in §6 of the AiP package.

**Lessons from "Zhifei" (Navigation Brilliance / Brinav):** The 2025 Xinhua report cites 3,000+ collision-avoidance simulation tests at the 220 NM² Nv Island test area in cooperation with the China Waterborne Transport Research Institute and the Shandong MSA, producing a stated 0.02% error rate over 1M+ autonomous decisions. Translation for your evidence package: (a) a quantified large-N simulation campaign is now an established, surveyor-recognised line of evidence in China; (b) pair simulation with a designated test area where possible; (c) 0.02% is an aspirational anchor but the MoU/TA path with DNV (announced 2024) shows that classification societies want process+product evidence — you cannot simulation your way to AiP without HAZID, V&V plan, and operator HMI evidence.

### 7. Open-source licensing (Q7) — verified

| Tool | Repo | License | Commercial use OK? |
|---|---|---|---|
| `dnv-opensource/maritime-schema` | github.com/dnv-opensource/maritime-schema | **MIT** (verified, "Distributed under the MIT license", LICENSE in repo) | **Yes** |
| `dnv-opensource/ship-traffic-generator` (`trafficgen`) | github.com/dnv-opensource/ship-traffic-generator | **MIT** (verified at top of GitHub repo "MIT license") | **Yes** |
| `open-simulation-platform/libcosim` | github.com/open-simulation-platform/libcosim | **MPL-2.0** (weak copyleft — file-level: modifications to MPL files must be shared, but linking/embedding in proprietary apps is fine) | **Yes**, with file-level disclosure of any modifications you make to libcosim itself |
| `nikpau/mmgdynamics` | github.com/nikpau/mmgdynamics | License field: README does not explicitly cite a SPDX identifier in the snippets returned; the repository top-level shows a permissive style; **action:** open an issue or PR to confirm and pin the SHA. **Treat as "license pending verification" until a LICENSE file is reviewed in-tree.** | Likely yes (academic permissive), but **do not ship without confirmation** |
| `NTNU-TTO/colav-simulator` | github.com/NTNU-TTO/colav-simulator | License not surfaced in search; Open-sourced autumn 2025 by NTNU. **Action:** check repository LICENSE before shipping; treat as research-license-pending. Use only in the RL workbench (non-certified path) initially. | TBD — verify before commercial shipping |
| `EivMeyer/gym-auv` | github.com/EivMeyer/gym-auv | No LICENSE surfaced in canonical EivMeyer fork; original `camillasterud/gym-auv` likewise unspecified. **Default GitHub rule: no license = all rights reserved.** | **No, until license is granted/clarified.** Use only as a reference for reward-shaping and observation design; do not vendor code. |
| `dnv-opensource/farn` | github.com/dnv-opensource/farn | **MIT** | **Yes** |
| `dnv-opensource/ospx` | github.com/dnv-opensource/ospx | **MIT** | **Yes** |
| `dnv-opensource/mlfmu` | github.com/dnv-opensource/mlfmu | LICENSE file present in repo (DNV pattern is consistently MIT for these tools) — verify the LICENSE file directly | **Yes** (assuming MIT consistent with sister DNV tools; verify) |

**Net licensing posture:** The DNV ecosystem (maritime-schema, ship-traffic-generator, farn, ospx, mlfmu) and OSP libcosim are commercial-ready. The MMG and gym-auv components require license confirmation before any commercial shipping; in the meantime, restrict them to the RL workbench / research silo.

---

## Details

### Component Map (Tool → SIL Function)

- **Scenario authoring & schema:** `maritime-schema` (Pydantic models, JSON Schema validation) + project-specific `metadata.*` extension fields → all scenario files validated in pre-commit hook and in CI.
- **Scenario generation:** `trafficgen gen-situation` for parametric encounter sweeps; custom Python script for Imazu-22 hand-encoding; `farn` for 1,100-cell coverage cube case-folder generation.
- **AIS data adapters:** Custom ROS2 node reading historical NMEA / AISHub feeds → emits same internal IDL track topic as `world_model_mock`.
- **Ship dynamics:** `nikpau/mmgdynamics` Python core (or its `pythonfmu`/`mlfmu`-built FMU) wrapped behind C++ `ShipMotionSimulator` interface; calls `pstep()` per tick.
- **Co-simulation orchestration (boundary):** OSP `libcosim` (or `libcosimpy` for prototyping) when running multi-FMU graphs (e.g., FCB own-ship MMG + RL target FMU); `ospx` to author the OSP system structure.
- **ROS2 SIL kernel:** ROS2 Jazzy on Ubuntu 22.04 (note: ROS2 Jazzy targets Ubuntu 24.04; on 22.04 use Humble or build Jazzy from source — flag this in §Caveats), `launch_testing` for SIL tests, `gtest`/`gmock` for unit tests.
- **CI/CD:** GitLab CI driving `colcon build` + `colcon test`, parametric matrix from `farn`-generated case folders, evidence Parquet upload to artefact store.
- **RL workbench (separate repo silo):** `colav-simulator` (NTNU) as scenario harness, Gymnasium 1.x + Stable-Baselines3 / acados, ONNX export, `mlfmu` to package back.
- **Trained-policy re-import to certified SIL:** `mlfmu build → .fmu` → loaded by `libcosim` as a black-box target-ship FMU.
- **Visualisation/HMI bridge tests:** RViz2 + custom `m8_hmi_panel` for surveyor demos.

### Layer Interface Diagram (text)

```
                      ┌────────────────────────────────────┐
SIL HARNESS BOUNDARY  │ Scenario YAML (maritime-schema +   │
(Python/CI orchestr.) │ FCB extension) — farn case folder  │
                      └──────────────┬─────────────────────┘
                                     │  loads
   ┌─────────────────────────────────▼──────────────────────────────────┐
   │  ROS2 Jazzy SIL graph (production C++/MISRA binaries under test)   │
   │                                                                    │
   │  l1_world_model_mock ──/world_model/tracks (IDL #03)──► [M2 World] │
   │  voyage_stub        ──/voyage/plan       (IDL #11)──► [M3 Mission]│
   │  ais_replay/synth   ──/ais/contacts      (IDL #04)──► [M2 World]  │
   │                                                                    │
   │              [M1 ODD] [M2 World] [M3 Mission]                      │
   │                  │       │           │                             │
   │                  └──► [M4 Behaviour Arbiter — Mid-MPC] ────┐       │
   │                  └──► [M5 Tactical Planner — BC-MPC]    ───┤       │
   │                  └──► [M6 COLREGs Reasoner]            ────┤       │
   │                              │                             │       │
   │                              ▼                             │       │
   │                       [M7 Doer-Checker] ◄──────────────────┘       │
   │                              │                                     │
   │                              ▼                                     │
   │                       /cmd/setpoint  (IDL #18)                     │
   │                              │                                     │
   │                              ▼                                     │
   │  [M8 HMI Bridge] ─/hmi/explain (IDL #21)──► RViz / surveyor view   │
   └──────────────────────────────┬─────────────────────────────────────┘
                                  │ /cmd/thrust /cmd/rudder
                                  ▼
   ┌──────────────────────────────────────────────────────────────────┐
   │  ship_dynamics_node (FCBPlugin: 4-DOF≤12kn / 6-DOF>12kn)         │
   │   wraps mmgdynamics.pstep()  — own-ship                          │
   │  target_ship_pool_node — N× FCBPlugin instances OR N× target_   │
   │   policy.fmu loaded via libcosim (Stage 3 RL re-import)          │
   └──────────────────────────────┬───────────────────────────────────┘
                                  │ /own_ship/state, /target/N/state (IDL #07)
                                  ▼
                         (closes the loop into M2)

   Recorded out-of-band per tick:  CagaTimeStep Apache-Arrow table →
   evidence/<scenario_id>.parquet  + traceability CSV row
```

### CCS Evidence Checklist mapped to Rules for Intelligent Ships 2024

(Numbering follows §6 above; each artefact must be reproducible by replaying the scenario YAML at the recorded git SHA.)

1. ConOps & ODD — clause family: Intelligent Navigation Functional Definition.
2. HAZID register — clause family: Risk Assessment for Intelligent Functions.
3. Software development plan — clause family: Software Quality Assurance.
4. Doer-Checker architecture & formal coverage proof — clause family: Functional Safety Architecture.
5. Coverage cube execution report (Imazu-22 mandatory, 1,100-cell parametric, Brinav-precedent scale) — clause family: Verification & Testing of Intelligent Navigation.
6. Simulation model assurance per DNV-RP-0513 (model qualification, verification, validation against sea-trial data).
7. KPI / pass-criteria report with self-justification.
8. Cyber-resilience evidence per IACS UR E26 + E27.
9. HMI/transparency report (M8).
10. Configuration management trace.

### RL Boundary — code-level interface specification

**Production side (certified):**
```cpp
// Used by ROS2 ship_dynamics_node and target_ship_pool_node
class TargetPolicy {
public:
  virtual ~TargetPolicy() = default;
  virtual Cmd act(const ObservationVec& obs, double t) = 0;
};

class ScriptedTargetPolicy : public TargetPolicy { /* deterministic from YAML */ };
class FmuTargetPolicy      : public TargetPolicy { /* libcosim slave wrapping mlfmu-built FMU */ };
```

**RL workbench side (non-certified):**
```python
# rl_workbench/envs/fcb_colav_env.py
class FCBColavEnv(gym.Env):
    """Scenario YAML + Python MMG dynamics + Python COLREGs feature extractor.
       Shares scenario format and vessel_params.yaml with the SIL kernel.
       NEVER imports rclpy or any l3_tdl_kernel package."""
```

When the RL trainer produces `target_policy.onnx`, the build pipeline runs:
```
mlfmu build --interface-file iface.json --model-file target_policy.onnx
# produces target_policy.fmu, signed and stored as evidence artefact
```
which is then loaded by the certified SIL via `libcosim` — preserving the certification chain because the binary that reads the trained weights is the audited mlfmu/libcosim runtime, not Python.

---

## Recommendations (staged, with thresholds for change)

**Stage 0 (next 6 weeks) — foundations.** Stand up the ROS2 Jazzy SIL kernel skeleton with M1–M8 stubs; integrate `maritime-schema` validation in pre-commit; vendor `nikpau/mmgdynamics` after license confirmation; write the FCB plugin with the 4-DOF/6-DOF mode switch; encode Imazu-22 in YAML; achieve green CI on Imazu-22 with placeholder M4/M5 logic. **Move to Stage 1 when:** Imazu-22 runs end-to-end in CI in <30 min wall-clock.

**Stage 1 (months 2-4) — coverage cube + evidence pipeline.** Add `farn` case-folder generation; integrate `trafficgen gen-situation`; build the Parquet/CSV evidence emitter; integrate `launch_testing` for pass/fail assertions; complete M6 COLREGs reasoner Rules 5–19; harness DNV-RP-0513 model-assurance documentation against captive sea-trial data on a scaled FCB. **Move to Stage 2 when:** 1,100-cell cube runs nightly with ≥99% pass on benign scenarios and per-cell evidence is auto-generated.

**Stage 2 (months 4-7) — CCS AiP submission package.** Author the AiP dossier (10 artefact items), engage CCS Beijing technical centre early (precedent: Brinav's DNV/CCS dual track), perform a HAZID workshop with operator stakeholders, complete IACS UR E26/E27 cyber documentation, freeze v1.0 of scenario library and vessel_params. **Move to Stage 3 when:** AiP feedback received and gap items closed.

**Stage 3 (months 7-12) — RL extensibility.** Stand up `rl_workbench` repo silo with `colav-simulator`-style backend; train target-ship adversaries; export ONNX → `mlfmu` → FMU; integrate via `libcosim` into the certified SIL **without modifying any M1–M8 code**. **Success criterion:** a regression test where a libcosim-loaded RL target FMU replaces a scripted target in 50 cube cells with no kernel rebuild, and pass rates remain unchanged.

**Decision triggers to revisit Option D:**
- If CCS surveyors specifically request a Modelica/FMI-pure submission (>30% of the kernel must move to FMUs), reconsider Option A.
- If RL adversaries become the dominant test driver (>50% of CI), promote the Gymnasium harness to first-class and re-evaluate Option C.

---

## Caveats and flagged risks (require expert consultation or further verification)

1. **ROS2 Jazzy on Ubuntu 22.04 mismatch.** ROS2 Jazzy is officially targeted at Ubuntu 24.04 Noble; Humble is the LTS for 22.04. Either upgrade the host to 24.04 (preferred for Jazzy) or switch to Humble; the architecture above is unchanged either way. **Confirm with project lead before sprint 0.**
2. **License gaps to close before commercial shipping:** `nikpau/mmgdynamics`, `EivMeyer/gym-auv`, `NTNU-TTO/colav-simulator`. The EivMeyer gym-auv repo has *no* LICENSE in the canonical repo on the snippets returned — under GitHub's default, this means all rights reserved. Treat as reference-only; do not vendor.
3. **CCS i-Ship N1/N2/N3 numerical mapping:** the search returned the 2024 rule set and i-Ship symbol family (e.g., i-Ship (I, M, No)) but did not surface a verbatim N1/N2/N3 hierarchy mapping in plain text. The user's question presumes N1/N2/N3; in CCS practice the navigation notation is "(N)" with sub-qualifiers (e.g., open-water autonomous navigation `No`). **Action:** procure CCS RIS 2024 (Chinese + English editions) and verify the exact notation grammar before the AiP submission.
4. **Imazu-22 canonical reference data** is not distributed as a single machine-readable artefact; reconstructions vary slightly between authors (Hasegawa/Cai vs Sawada vs others). Freeze your encoded set as `imazu22_v1.0.yaml` and treat any future change as a regression-baseline change requiring re-evaluation.
5. **CPA/TCPA pass thresholds are self-justified, not standard-prescribed.** Document the justification carefully; expect surveyor pushback. The 300-500 m FCB CPA threshold is defensible from operator practice; do not over-promise (Brinav's reported 0.753 NM ≈ 1,400 m is on a ~110 m container ship, not a 45 m FCB).
6. **The "Zhifei" 0.02% error-rate figure** comes from a Xinhua/People's Daily corporate-source report (Brinav/Navigation Brilliance) and is not independently audited; cite as industry precedent and motivation, not as a benchmark target.
7. **DNV-RP-0513 covers first-principles models only**; data-driven (RL/ML) models fall under DNV-RP-0510 / DNV-RP-0665. Plan two parallel assurance streams when Stage 3 begins.
8. **AROS notations (DNV, January 2025)** and CCS RIS are not formally harmonised; if dual-class is desired, run gap analyses against both sets early — their categorisations of "supervised autonomy" vs CCS "open-water autonomous navigation" are similar but not isomorphic.
9. **MMG empirical-coefficient quality varies sharply between vessels** (per the `mmgdynamics` README disclaimer). For a 45 m semi-planing FCB you cannot rely on `kvlcc2_l64`-style pre-calibrated tankers; budget for captive-model tests or CFD-derived coefficients, and the planing-mode (>12 kn, 6-DOF) extension is *not* part of the standard MMG — it must be implemented (likely via a Savitsky/semi-empirical hybrid) and its assurance documented separately under DNV-RP-0513 §validation.
10. **Open question for expert consultation:** Whether CCS will accept maritime-schema (a DNV-originated open format) as an evidence container, or require Chinese-format equivalents. Engage CCS technical centre via the Brinav-precedent route early.