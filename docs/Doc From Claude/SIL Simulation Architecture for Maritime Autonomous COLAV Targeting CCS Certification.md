# SIL Simulation Architecture for Maritime Autonomous COLAV Targeting CCS Certification

- Build your Stage-1 SIL stack on the **DNV open-source maritime-schema + ship-traffic-generator + Open Simulation Platform (OSP, FMI-based) + a Python MMG core (Yasukawa & Yoshimura 2015, J. Mar. Sci. Technol. 20:37–52, doi:10.1007/s00773-014-0293-y)**, wrapped in YAML/JSON-declared scenarios that map 1:1 onto a coverage cube of COLREG Rule × ODD × Disturbance — this is the only stack today that is simultaneously open-source, commercially usable, FMI-conformant, and already being accepted by a Tier-1 class society as V&V evidence (DNV's `maritime-schema` is explicitly aimed at "developers, test engineers, researchers and *regulators*"). For CCS, this evidence is then mapped onto **CCS Rules for Intelligent Ships 2024** (autonomous-navigation notations N1/N2/N3 and remote-control R1/R2) and the **CCS Guidelines for Autonomous Cargo Ships (2018)**, supplemented by **DNV-CG-0264 (2024 ed.) + DNV-RP-0513 (Assurance of simulation models) + DNV-RP-0510/0671 (data-driven & AI-enabled systems)** as the de-facto international gap-filler — CCS has signed a 2024 MoU with DNV/Brinav explicitly to converge on this approach.
- For the 1000+ scenario library, anchor on the **Imazu 22-case canonical set + the COLREG Rules 5–10 / 13–19 grid**, parameterise via Sobol sampling (Hassani et al. 2022, Ocean Eng. paper from the EU AUTOSHIP project), and target a coverage-cube of **11 rules × 4 ODD subdomains × 5 disturbance levels = 220 cells × ~5 seeds ≈ 1100 concrete cases**; the 60:25:15 adversarial:nominal:boundary split has *no* peer-reviewed industry source — treat it as an internal heuristic, not a referenced standard, and document it as such in your V&V plan.
- Architect the three stages as **separate processes communicating only through the maritime-schema YAML (Stage 1) → GUI-emitted YAML (Stage 2) → Gymnasium `step()`/`reset()` adapter that re-uses the same MMG FMU but lives in a non-certified container (Stage 3)**. Keep the safety-critical SIL kernel (MMG dynamics, scenario executor, COLREGs evaluator, evidence logger) in a frozen, version-tagged repo subject to DNV-RP-0513 model-assurance; let RL training, scenario fuzzing and adversarial generation live *outside* that boundary and only consume/produce schema-valid scenarios. This is exactly the pattern used by SFI-AutoShip / NTNU `colav-simulator` (github.com/ntnu-itk-autonomous-ship-lab/colav-simulator) and PyGemini (Vasstein et al., arXiv:2506.06262), and it is the only architecture that survives a CCS audit while still letting you do RL.

---

## Key Findings

**1. There is no off-the-shelf "certified maritime SIL platform" for L3 COLAV.** Kongsberg K-Sim (Navigation/Engine/Cargo) is the only commercial maritime simulator family certified by a class society (DNV ST-0033 Classes A/B/C/D), but that certification is for **STCW seafarer training**, *not* as a verification tool for autonomous navigation software. K-Sim is used as a backend by autonomy projects (Yara Birkeland, AUTOSHIP) but the certification path of any algorithm tested in K-Sim still requires its own DNV-CG-0264 / CCS evidence package. ANSYS STK and OpenMETA are *not* maritime-domain tools and have no track record with IACS members. MARINTEK ShipSim (now SINTEF Ocean's VeSim/SIMA) is widely used in Norwegian academic projects but is also not class-approved as a V&V tool per se.

**2. The de-facto industrial open-source stack is Norwegian-led and converging.** The relevant building blocks are:
- **Open Simulation Platform (OSP)** — JIP between DNV, Kongsberg Maritime, SINTEF Ocean and NTNU since 2018, FMI-2.0/3.0 based co-simulation, MIT-licensed (opensimulationplatform.com, github.com/open-simulation-platform). FMI is the bridge that lets MMG, sensor models, and control software co-simulate without IP exposure.
- **DNV maritime-schema** (github.com/dnv-opensource/maritime-schema, MIT) — open YAML/JSON schema for "traffic situations" (own-ship + target-ship paths) and "solutions" (COLAV outputs); explicitly positioned for "system developers, test engineers, researchers, and regulators."
- **DNV ship-traffic-generator** (github.com/dnv-opensource/ship-traffic-generator) — generates schema-valid encounters via CLI `trafficgen gen-situation`, Python 3.11+, MIT.
- **DNV `farn` + `ospx` + `dictIO`** — n-dimensional case-folder generation and OSP/FMU case building, exactly what you need for the coverage cube.
- **DNV `mlfmu`** — wraps ONNX ML models as FMUs; the route to bring an RL-trained policy back into the certified SIL loop without polluting it.
- **NTNU SFI-AutoShip `colav-simulator`** (github.com/ntnu-itk-autonomous-ship-lab/colav-simulator) — open-sourced autumn 2025, YAML scenario configs validated by Cerberus, already wraps to Gymnasium/Stable-Baselines (the canonical reference architecture for what you're building); described in CCTA 2023 paper by Pedersen, Glomsrud et al.
- **PyGemini / Autoferry Gemini** (Vasstein et al., arXiv:2506.06262, 2025) — Python-native, Configuration-Driven-Development, explicitly built for assurance cases, builds on Unity for sensor simulation.
- **`mmgdynamics`** (github.com/nikpau/mmgdynamics) — clean Python implementation of the Yasukawa & Yoshimura 2015 MMG standard with KVLCC2-class calibrated vessels, currents, winds, shallow water; permissive license. Good base for plugin pattern.

**3. For an FCB (semi-planing, 45 m, 18–25 kn) the standard MMG (Yasukawa & Yoshimura 2015, doi:10.1007/s00773-014-0293-y) is borderline.** Standard MMG was derived for displacement hulls (KVLCC2, container ships) and is documented to be inaccurate for medium-speed vessels by Ishiguro et al. (1996) and others. For semi-planing operation at Fr > 1, you should:
- Use the 4-DOF MMG with roll-coupling extension by Yasukawa, Sakuno & Yoshimura, J. Mar. Sci. Technol. 24:1280–1296 (2019), doi:10.1007/s00773-019-00625-4, in displacement mode (≤ ~14 kn).
- Switch to a **6-DOF planing-craft model** (Katayama et al., J. Navig. Port Res. 40:231–239, 2016; Algarín & Bula 2021) above Fr ≈ 1, with empirical hydrodynamic-coefficient blending. Sadati, Zeraatgar & Moghaddas (Proc. IMechE Part M, 2022, doi:10.1177/14750902211030386) provide validated full-scale planing-craft turning data — use as parameter-uncertainty benchmark.
- The new empirical-coefficient formulas in Sciencedirect S0029801825015379 (Ocean Eng. 2025) extend MMG to "medium-speed and fishing vessels" and are likely the closest published match; cite explicitly in your model-validation report.
- Parameterise via YAML using the `mmgdynamics` schema as a starting template; bound each hydrodynamic derivative with ±20–30 % uncertainty (typical captive-test scatter) and run a Sobol-based sensitivity analysis (the same library DNV `farn` uses) per **DNV-RP-0513 Sec. 4 Model assurance**.

**4. CCS-specific certification path is concrete but goal-based.** The applicable CCS documents are:
- **CCS Rules for Intelligent Ships, 2024 edition** (released 1 Apr 2024, ccs.org.cn/ccswzen/specialDetail?id=202405080720118587) — defines the eight smart-function notations including intelligent-navigation **N1 (decision support, manned), N2 (remote control / supervised autonomy), N3 (full autonomous in open waters or whole voyage)** per Article 9.1.2; remote-control **R1/R2**.
- **CCS Guidelines for Autonomous Cargo Ships, 2018** (ccs.org.cn/ccswzen/articleDetail?id=201910000000003792) — first jurisdictional rulebook for unmanned cargo ships globally; basis for AiP of *Jindouyun-0* (2019, Yunzhou Tech, Zhuhai) and *Zhifei* (2022, 300-TEU, Qingdao, Navigation Brilliance / Brinav). The Zhifei case is the most useful Chinese precedent: per Xinhua (Feb 2025), the program completed "over 3 000 collision-avoidance simulation tests under complex meteorological conditions … error rate 0.02 %" before sea trials, in cooperation with the China Waterborne Transport Research Institute and CCS.
- CCS does not publish a tool-qualification standard analogous to ISO 26262 TCL-3. In practice, CCS approval-in-principle for autonomy submissions has been granted on a goal-based, **MSC.1/Circ.1455 ("Approval of Alternatives and Equivalents")** baseline, which DNV-CG-0264 also adopts. CCS therefore *de facto* accepts evidence packages structured around DNV-CG-0264 + DNV-RP-0513 + IEC 61508 SIL principles. The Sept 2024 CCS–DNV–Brinav MoU formalises this convergence.

**5. IACS UR E26 / E27 are cyber-resilience (not navigation-functional) requirements.** They became applicable to ships contracted on or after 1 July 2024 (after the original 1 Jan 2024 versions were withdrawn and revised — IACS press release, June 2024). They apply to OT/CBS in scope ≥500 GT and require a "Cyber Resilience Test Procedure" covering construction, commissioning and annual surveys. For an L3 COLAV, your SIL test plan must therefore include an explicit cybersecurity test sub-suite (sensor-spoofing scenarios, malformed AIS, network-failure modes); the GPS-spoofing DRL paper at DTU (orbit.dtu.dk) is a useful citable design reference. **UR E26/E27 do not qualify the simulator itself** — that gap is filled by DNV-RP-0513 and DNV-RP-0671 (Assurance of AI-enabled systems) on the international side, and remains undocumented in CCS rules.

**6. Scenario-library industry practice (1000+ cases).** The closest published precedents:
- Pedersen, Glomsrud et al., *Towards simulation-based verification of autonomous navigation systems*, Safety Science 129:104799 (2020), doi:10.1016/j.ssci.2020.104799 — the canonical Norwegian reference, defines a scenario-manager + test-evaluator separation and uses COLREG Rule 6 ("safe speed") as the safety-margin anchor.
- Hassani et al., *Automatic traffic scenarios generation for autonomous ships collision-avoidance system testing*, Ocean Eng. (2022), doi:10.1016/j.oceaneng.2022.111864 (EU AUTOSHIP project) — uses **Sobol sequences** to sample traffic parameters, applies geometric COLREG-rule filters to extract hazardous subset, then clusters by risk vector to compress thousands of raw cases into a manageable representative test suite. This is the exact methodology you should adopt.
- Sun, Zhao, Deng & Fu, A*STAR IHPC, *From Vessel Trajectories to Safety-Critical Encounter Scenarios*, arXiv:2603.28067 (2026) — VAE on AIS data, complements knowledge-driven cube with naturalistic encounters.
- Frey, Kargén & Varró, *Assessing Scene Generation Techniques for Testing COLREGS-Compliance of Autonomous Surface Vehicles*, Proc. ACM Software Eng. 2(ISSTA):985-1008 (2025), doi:10.1145/3728919 — most recent academic survey of scene-generation methods.
- Krasowski, Schärdinger, Arcak & Althoff, *Intelligent Sailing Model for Open Sea Navigation*, arXiv:2501.04988 (2025) — proposes ISM as a "standard for challenging and realistic maritime traffic simulation"; tested 4 049 critical scenarios with ~97 % goal-reach rate. Best benchmark target.
- The **Imazu problem** (22 canonical 2/3/4-ship encounters; Imazu 1987, popularised in Sawada et al., J. Mar. Sci. Technol. 26 (2021)) is the universally accepted minimum benchmark — every certifiable COLAV system must pass all 22.

**7. The 60:25:15 adversarial:nominal:boundary ratio is not a published industry standard.** Neither IACS, IMO MASS Code drafts, DNV-CG-0264, the AUTOSHIP deliverables, nor the Applied Intuition automotive whitepapers prescribe such a split. Automotive practice (ISO 34502, NHTSA scenario libraries) typically frames coverage as "deductive (requirement-driven) ∪ inductive (data-driven)" with the *adversarial* slice generated post-hoc by RL/optimisation methods (FREA, AuthSim, AV-Fuzzer in arxiv literature). Treat 60:25:15 as a **defensible internal allocation** and justify it in your test plan by referencing (i) the requirement-coverage minimum (every COLREG rule × every ODD subdomain × every disturbance level — that fills your Nominal+Boundary 40 %) and (ii) ALARP-style stress-testing for the Adversarial 60 %. Do not present it to CCS as derived from an external standard — that claim is not supportable.

**8. Stage-3 RL must be architecturally segregated from the certified SIL kernel.** Existing maritime Gymnasium environments — `gym-auv` (EivMeyer/gym-auv: PPO + LiDAR + 2D maneuvering for ASVs; Meyer et al., arXiv:1912.08578 / arXiv:2006.09540), `gym-usv`, `MarineGym` (Chu et al., arXiv:2410.14117 / 2503.09203, 250 000 FPS on Isaac Sim, but UUV-focused), `Aeolus Ocean` (arXiv:2307.06688), `ShipAI` (jmpf2018/ShipAI), and the `colav-simulator` Gymnasium wrapper — all share the same anti-pattern from a certification standpoint: the RL training loop directly mutates simulator internals. The clean architectural pattern, demonstrated in PyGemini (arXiv:2506.06262) and SFI-AutoShip, is:
  - **Certified layer (frozen, DNV-RP-0513 assured):** MMG FMU + scenario executor + maritime-schema validator + evidence logger.
  - **Gym adapter layer (non-certified):** thin Python wrapper exposing `step()`/`reset()`/`observation_space`/`action_space`, calling the FMU through OSP/FMI sockets only.
  - **RL training layer (Stable-Baselines3 / RLlib / CleanRL):** lives in a Docker container that has read-only access to the certified layer's binaries.
  - **Sim-to-real bridge (`mlfmu`):** trained ONNX policy is wrapped as an FMU, re-imported into the certified layer for V&V — at this point the policy itself becomes subject to **DNV-RP-0671 (Assurance of AI-enabled systems)** and DNV-RP-0510 (data-driven algorithms).
  Sim-to-real precedent: Sim2Sea (Cui et al., AAMAS 2026, arXiv:2603.04057, doi:10.65109/CMRP6518) demonstrated zero-shot transfer to a 17-ton USV in congested waters using domain randomisation; Batista et al. arXiv:2407.08263 reports 13.1 % energy and 7.4 % time reduction via system-ID + domain-randomisation. Phase-4 adversarial scenario generation (FREA, AuthSim style) sits on top of this — the RL adversary proposes new scenarios that, after passing schema validation, are appended to the certified scenario library.

**9. ASAM OpenSCENARIO has no formal maritime adaptation.** OpenSCENARIO XML 1.x and DSL 2.x are road-domain standards; there is no published maritime profile. The Norwegian ecosystem instead uses `maritime-schema` as a *de facto* equivalent — strategically wiser to adopt that than to retrofit OpenSCENARIO. For Stage-2 GUI editors targeting `maritime-schema` YAML, no widely-adopted commercial product exists; you will likely build in-house using Streamlit/React + `pydantic` validators (the same approach `ship-traffic-generator` uses). The Foretellix Foretify and Applied Intuition Object-Sim editors are road-domain, but their *abstraction-pattern* (concrete → logical → abstract scenarios) is directly transferable.

---

## Details

### 1. Industrial SIL platforms and their certification status

| Platform | Source | License | Class accepted as V&V? |
|---|---|---|---|
| Kongsberg K-Sim Navigation/Engine/Cargo | Commercial | Proprietary | DNV ST-0033 Class A/B/C/D — *for STCW training only*, not autonomy V&V |
| SINTEF VeSim / SIMA | SINTEF Ocean | Commercial / academic | Used in DNV/SINTEF JIPs, no class certificate as V&V |
| Open Simulation Platform (OSP) | DNV/KM/SINTEF/NTNU | MIT (FMI-based) | Reference platform in DNV joint research; DNV-RP-0513 compatible |
| DNV maritime-schema + ship-traffic-generator | DNV | MIT | Authored by class society; explicitly aimed at regulators |
| NTNU `colav-simulator` (SFI-AutoShip) | NTNU | Open (autumn 2025) | Used in CCTA 2023 paper, not yet class-accepted as a tool |
| PyGemini | NTNU | Permissive | Designed for assurance cases (insurers/regulators); pre-class |
| `mmgdynamics` | nikpau (TUHH) | Permissive | Reference implementation only |
| `gym-auv`, MarineGym, Aeolus Ocean | Academic | MIT / similar | Research; never certified |
| MOOS-IvP | MIT | Open | Used for hardware autonomy stacks, not as a V&V simulator |
| ANSYS STK, OpenMETA | Commercial | Proprietary | No maritime certification, no maritime track record |

**Tool-qualification analog:** Maritime has no published TCL-3-equivalent. The closest references are **DNV-RP-0513 (Assurance of simulation models)** — the only document worldwide that prescribes (1) risk evaluation of model use, (2) supplier maturity assessment, (3) product assurance (conceptual qualification + verification + validation against reality) — and **DNV-RP-0671 (Assurance of AI-enabled systems)** for the RL/ML layer. CCS has not published an equivalent; in practice, the CCS surveyor will audit against the principles of DNV-RP-0513 supplemented by IEC 61508 SIL-2/3 process evidence.

### 2. Scenario / scene management for 1000+ SIL test cases

**Recommended cube:**
- **Rule axis (11):** COLREG Rules 5 (look-out), 6 (safe speed), 7 (risk of collision), 8 (action to avoid), 9 (narrow channels), 13 (overtaking), 14 (head-on), 15 (crossing), 16 (give-way), 17 (stand-on), 18 (responsibilities between vessels) + Rule 19 (restricted visibility) covered separately.
- **ODD subdomain axis (4):** open-sea / coastal / restricted-channel / harbour-approach.
- **Disturbance axis (5):** wind sea-state 0/3/5/6 + current ±2 kn + sensor-noise σ.
- **Multiplier:** 220 cells × 5 random seeds = 1100 concrete scenarios per the AUTOSHIP / Hassani 2022 Sobol-sequence methodology.

**Evidence schema (per scenario, in YAML using maritime-schema):**

```yaml
scenario_id: SC-R14-OD2-D3-s17
hazid_ref: HAZID-NAV-014
colreg_rules: [14]            # head-on
odd_subdomain: coastal
disturbance:
  sea_state: 5
  current_kn: 1.5
  ais_dropout_pct: 10
own_ship: { model: FCB-45m-MMG6DOF, speed_kn: 22, … }
target_ships: [ … ]
expected_outcome:
  cpa_min_m: 200
  colreg_compliance: true
  rule14_starboard_alteration_deg: ">=10"
```

This file is the certification artefact: every test report links scenario_id → hazid_ref → COLREG rule → ODD cell → result, satisfying CCS's traceability expectation per the `Rules for Intelligent Ships 2024` evidence chain (Sec. 9 / functional notations).

**Batch execution:** GitHub Actions / GitLab CI with 32-way parallelism, FMU-proxy distributed co-simulation per OSP examples; per-run wall-clock ~60-120 s for a 600-s scenario at 20 Hz on commodity hardware → full 1100-case suite in ~1-2 hours.

**Ratio (60:25:15) caveat — important:** Mark as "internally adopted heuristic; literature reports no consensus split. AV literature instead emphasises requirement-coverage completeness (deductive) plus naturalistic + adversarial generation (inductive). Hao et al., *Adversarial Safety-Critical Scenario Generation using Naturalistic Human Driving Priors*, arXiv:2408.03200, demonstrates +44 % efficiency from adversarial-naturalistic blending." Keep the 60 % adversarial slice methodologically defensible by deriving it from FREA / AuthSim style RL fuzzers operating on top of nominal cases, not as an arbitrary policy choice.

### 3. Ship digital model (MMG) for Fast Crew Boat

**Reference paper:** Yasukawa H., Yoshimura Y., *Introduction of MMG standard method for ship maneuvering predictions*, J. Mar. Sci. Technol. 20:37-52 (2015), doi:10.1007/s00773-014-0293-y.

**Recommended FCB modelling stack:**
1. **Low-speed regime (≤ Fr 0.7, ~12 kn for 45 m FCB):** 4-DOF MMG with roll-coupling per Yasukawa, Sakuno & Yoshimura, J. Mar. Sci. Technol. 24:1280-1296 (2019), doi:10.1007/s00773-019-00625-4.
2. **Transition + planing regime (Fr 0.7–1.7, 12-25 kn):** 6-DOF planing model per Katayama et al. (2016) and Algarín & Bula (J. Ocean Eng. 221, 2021), with empirical-coefficient blending; validated against Sadati et al. (2022) full-scale data.
3. **Implementation:** start from `nikpau/mmgdynamics` as the open-source MMG core; define an **abstract `ShipMotionSimulator` interface** with `pstep(state, control, env) -> next_state` signature (mirroring mmgdynamics' API) and an **`FCBPluginSimulator`** that switches between (1) and (2) on Fr threshold.
4. **YAML parameter file** (per-vessel, version-controlled):

```yaml
vessel: FCB-45m
hull:
  loa_m: 45.0
  beam_m: 8.6
  draft_m: 2.1
  block_coeff: 0.42
  hull_class: semi-planing
mass:
  displacement_t: 280
  Ixx: 1.2e6 ; Iyy: 4.5e7 ; Izz: 4.8e7
hydrodynamics:                # MMG derivatives ± 1-σ uncertainty
  Xvv:  -0.041 ± 0.008
  Yv:   -0.265 ± 0.040
  Nr:   -0.066 ± 0.010
  …
propulsion: { … }
rudder: { … }
fr_blend_threshold: 0.85
```
5. **Sensitivity analysis & uncertainty bands:** run Sobol global sensitivity on the YAML uncertainty bands using `farn` + the SALib package; satisfies DNV-RP-0513 §4.4 (model validation with quantified uncertainty). Cross-check zigzag and turning-circle outputs against ITTC procedure 7.5-02-06-02 free-running data if available.

### 4. Stage-2 GUI editor roadmap

**Architectural rule:** GUI never replaces YAML — it produces YAML. The certified evidence chain remains "YAML file → schema validation → executor → evidence log."

Recommended path:
- **MVP:** Streamlit/Dash app with a 2-D map (Folium/Leaflet over ENC tiles via `seacharts`) and form widgets bound to `pydantic` models that already exist in `ship-traffic-gen`. Output: maritime-schema YAML + auto-generated screenshot for the test report.
- **Mid-term:** React + DeckGL editor; integrate Imazu-22 + AIS-derived templates as drag-and-drop primitives.
- **CI/CD invariance:** GUI-emitted YAML must pass *exactly the same* JSON-Schema validator (`maritime-schema` v0.0.7+) as hand-written YAML. The CI pipeline (lint → schema-validate → executor → evaluator → coverage matrix) is unchanged. This guarantees the certification chain is not broken by a GUI rev.
- **Why not OpenSCENARIO XML/DSL?** No maritime profile; ASAM tooling chain assumes road-domain entities (lanes, traffic lights). Foretellix Foretify and Applied Intuition Object-Sim are excellent for ADAS but would require a maritime extension you'd own and maintain. Stick with maritime-schema until ASAM publishes an OpenSCENARIO-Maritime profile (no announcement as of May 2026).

### 5. Stage-3 RL training interface architecture

**Boundary diagram (top of stack to bottom):**
```
[ Adversarial scenario generator (RL, FREA/AuthSim-style) ]    ← non-certified
                  │ produces YAML
                  ▼
[ Stable-Baselines3 / Gymnasium agent ]                         ← non-certified
                  │ step()/reset()
                  ▼
[ Gymnasium adapter (thin) ]                                    ← non-certified
                  │ FMI-2.0 sockets via OSP cosim-cli
══════════════════════════════════════════════════════════════ ← assurance boundary
[ Certified SIL kernel : MMG-FMU + scenario executor +
  maritime-schema validator + evidence logger ]                 ← DNV-RP-0513 assured
                  ▲
                  │ ONNX→FMU via mlfmu
[ Trained policy re-imported as FMU for V&V ]                   ← then DNV-RP-0671/0510
```

**Reference precedents:**
- NTNU `colav-simulator` Gymnasium wrapper (autumn 2025).
- PyGemini's "Configuration-Driven Development" (arXiv:2506.06262) explicitly separates simulation, autonomy, and assurance containers.
- Sim2Sea (Cui et al., AAMAS 2026, arXiv:2603.04057) — GPU-accelerated parallel simulator + dual-stream policy + velocity-obstacle action masking + domain randomisation; demonstrated zero-shot transfer to a real 17-t USV in Chinese congested waters. **Most relevant Chinese-context paper for a CCS submission.**
- DTU "DRL Framework for Robust Maritime Collision Avoidance Under GPS Spoofing" (orbit.dtu.dk) — Imazu generalisation + INS-anomaly detection.
- Meyer, Robinson, Rasheed & San, *COLREG-Compliant Collision Avoidance for USV Using DRL*, arXiv:2006.09540; *Taming an autonomous surface vehicle …*, arXiv:1912.08578 — Norwegian baseline.
- Chun et al. (multiple), Sawada et al. J. Mar. Sci. Technol. 26 (2021) — DRL with LSTM in continuous action spaces, validated on Imazu.
- Multi-agent: Wang & Zhao, *Multiple Ships Cooperative Navigation … MADDPG*, arXiv:2410.21290 — directly maps to PettingZoo for adversary-pool training.

**Phase-4 adversarial generation ↔ 1000+ SIL scenarios:** the RL adversary's job is to generate *new* schema-valid scenarios that are demonstrably hazardous (low CPA, COLREG-stress) and *feasible* (FREA / AuthSim avoid the "unfair-collision" anti-pattern). Successful adversarial scenarios are appended to the scenario library only after schema validation + naturalness check; this is the documented mechanism for closing coverage gaps detected during regression.

### 6. CCS certification evidence chain

**Applicable CCS documents (2024 baseline):**
- *CCS Rules for Intelligent Ships 2024* (1 Apr 2024 effective; PDF: ccs.org.cn/ccswzen/file/download?fileid=202405080712174186). Defines functional notations N1/N2/N3 (autonomous-navigation), R1/R2 (remote-control), and seven other smart functions.
- *CCS Guidelines for Autonomous Cargo Ships 2018*.
- *Action Plan on the Development of Intelligent Ships (2019-2021)* — MIIT/MoT joint policy.
- *Guidelines for the Construction of Intelligent Ship Standard System (Feb 2021)*.

CCS publicly references DNV-CG-0264, IMO MSC.1/Circ.1455, and IACS UR E26/E27 in its 2024 collaboration with DNV; the CCS–DNV–Brinav MoU (announced at SMM 2024) is the most concrete signal that **CCS will accept a DNV-style evidence package as the basis of a CCS AiP for L3 COLAV.**

**Concrete deliverables for a CCS AiP submission (recommended bundle):**
1. **Concept of Operations (ConOps)** with Operational Envelope per Rødseth, Wennersberg & Nordahl, J. Marine Sci. Technol. 26 (2021), doi:10.1007/s00773-021-00815-z — defines ODD subdomains, human-automation responsibilities, geographic limits.
2. **HAZID + STPA report** (System-Theoretic Process Analysis), per Glomsrud & Xie 2019 framework; includes hazard-IDs that map 1:1 to scenario_ids in the SIL library. ABS approved the MTI APExS-auto framework using STPA in Sept 2024 — direct precedent.
3. **Functional and System Requirements** traceable to COLREG rules and CCS Rules for Intelligent Ships Sec. 9 articles.
4. **Simulation V&V plan** referencing DNV-RP-0513 (model assurance) — covers MMG model qualification, sensor-model verification, scenario-coverage matrix, adversarial-naturalistic split, and pass/fail criteria (CPA, TCPA, COLREG-classification).
5. **Test reports** per scenario + aggregated coverage matrix (Rule × ODD × Disturbance) + statistical pass-rate summary; the *Zhifei* programme cites 3 000+ simulations with 0.02 % error rate as the precedent benchmark to beat.
6. **Cybersecurity Resilience Test Procedure** per IACS UR E26 (April 2022 / June 2024 revision); UR E27 type-approval evidence for any individual CBS.
7. **AI/ML assurance package** if RL is in the deployed loop — DNV-RP-0671 (Assurance of AI-enabled systems) + DNV-RP-0510 (data-driven algorithms) + DNV-RP-0665 (machine learning applications) + AMLAS (Hawkins et al., Univ. of York, arXiv:2102.01564) artefacts: training-data lineage, model card, test-set independence, OOD monitoring.
8. **Sea-trial protocol** — bridges SIL evidence to physical trials at the **Wanshan unmanned-ship test field, Zhuhai** (Asia's first; in operation since 2018) or the **Qingdao 220-NM² intelligent-ship test area** (Brinav/CCS cooperation, approved by Shandong MSA).
9. **Operating manuals + remote-operator training** evidence (CCS regards manning under the 2024 Rules as a binding obligation per Article 9.1.2).

---

## Recommendations

**Stage-gate roadmap (decision-ready):**

**Stage 1 — Foundation (weeks 0-12).** Adopt DNV maritime-schema + ship-traffic-generator + OSP/FMI + a 4-DOF MMG core for the FCB derived from `nikpau/mmgdynamics`. Build the CI pipeline (schema-validate → batch-execute via OSP → coverage matrix) and target **the Imazu 22-case set + 200 ODD-extended scenarios** as the v1.0 release. **Trigger to advance:** all 22 Imazu cases passing with CPA ≥ 200 m and COLREG-classification rate ≥ 95 % across ±20 % parameter uncertainty bands.

**Stage 2 — GUI front-end (weeks 8-20, parallel).** Build a Streamlit-based map editor that emits maritime-schema YAML. **Hard rule:** CI pipeline accepts only schema-validated YAML, regardless of origin. **Trigger to advance:** GUI- and code-generated YAML produce byte-identical executor outputs on a 50-scenario regression suite.

**Stage 3 — RL adapter (weeks 16-32).** Wrap the certified kernel as a Gymnasium environment in a separate Docker container. Reproduce a Meyer/Sim2Sea-style DRL baseline on the same 1100-case library. Train an adversarial-scenario generator (FREA-style) that proposes new YAML scenarios; human-curate those that pass naturalness checks into the library. **Trigger:** RL agent achieves ≥ Imazu 22 plus ≥ 90 % CPA-success on the open-sea slice; adversarial generator finds ≥ 20 novel low-CPA cases per week.

**Stage 4 — CCS AiP submission (months 9-15).** Compile the 9-deliverable bundle above. Engage a Tier-1 class society (DNV) early via the CCS-DNV-Brinav cooperation channel; treat their feedback as authoritative on DNV-CG-0264 / RP-0513 conformity. Submit to CCS Technology Centre for AiP under the *Rules for Intelligent Ships 2024* relevant N-notation.

**Benchmarks that should change the plan:**
- If MMG-derivative validation against captive-model or sea-trial data fails at ±20 % bands → escalate to 6-DOF planing model + CFD for the high-Fr regime; budget 2 extra months.
- If CCS publishes a tool-qualification standard during 2026 → re-baseline against it (currently no such document is announced; track CCS website monthly).
- If IMO MSC adopts the non-mandatory MASS Code in 2026 → re-map the evidence bundle against the MASS Code chapters (CCS has stated it will align per Article 9.1.2 interpretation).
- If RL policy is intended to be in the deployed control loop (not just training) → DNV-RP-0671 + IEC 22989 obligations apply; add 3-6 months for model-card + OOD-monitor work. **Strong recommendation: keep RL out of the deployed L3 loop for the first AiP — use it only for adversarial scenario generation. This shortens the certification path materially.**

---

## Caveats

- The **60:25:15 adversarial:nominal:boundary ratio is not supported by any peer-reviewed industry document** I could locate; treat any V&V plan that cites it as an external standard as factually incorrect. Document it as an internal heuristic with explicit justification.
- **CCS does not publish an English-language tool-qualification standard equivalent to ISO 26262 TCL-3 or DO-330.** Practitioners rely on DNV-RP-0513/0671 by analogy and on direct dialogue with CCS surveyors. Plan for ≥ 2 review iterations.
- DNV-CG-0264 has multiple editions (2018, 2021 referenced in TransNav, and the updated edition underpinning the AROS class notation announced at SMM 2024). Confirm which edition CCS will reference — the 2024 update is the safer baseline.
- The IACS UR E26/E27 timeline is non-trivial: the original Jan 2024 versions were withdrawn; the revised versions apply to ships contracted on or after 1 July 2024. Older project documentation that cites E26/E27 may reference a withdrawn version.
- Several papers I cited are **arXiv preprints or 2025-2026 publications** (Sim2Sea AAMAS 2026, *From Vessel Trajectories…*, Frey et al. ISSTA 2025, PyGemini, Patil et al., the *Intelligent Sailing Model*) — these are leading-edge research, not yet textbook or class-rule references, and their methodologies should be validated locally before being treated as best practice.
- The widely-quoted Zhifei statistic of "3 000 collision-avoidance simulations with 0.02 % error rate" originates from a Xinhua interview with Brinav's deputy GM (Feb 2025) and has not been independently verified through a peer-reviewed publication; cite as company-disclosed.
- MMG was designed for displacement hulls; **use for a 22-25 kn semi-planing FCB is at the edge of the model's documented validity** and requires explicit empirical validation (towing tank or sea-trial) before being submitted as certification evidence. This is the single largest technical risk in your stack.
- The CCS-DNV-Brinav MoU is a *cooperation* agreement, not a regulatory equivalence treaty. CCS retains sovereign discretion and can require additional Chinese-flag-specific evidence (e.g., MoT testing-area trials at Wanshan or Qingdao).
- "MARINTEK ShipSim" as listed in the original task is no longer an active product name; MARINTEK was merged into SINTEF Ocean in 2017 and its hydrodynamic suite is now distributed as VeSim, SIMA and ShipX. Adjust scope statements accordingly.