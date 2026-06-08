# M1-M8 GAP AUDIT DIGEST  stats={'modules': 8, 'cross': 5, 'flow': 4, 'candidates': 127, 'verified': 40} unverified=87

## ===== MODULES =====

### M1 — TDL scheduling hub and single authority for operational safety context (ADR-001). Publishes ODD zone/health/mode to gate all downstream behavior modul
  PUB /l3/m1/odd_state [PARTIAL_FIELDS] unpop:schema_version (stays default 0) :: odd_envelope_manager_node.cpp:926-970 on_odd_state_publish_tick() sets stamp/zone/health/envelope_state/scores/rationale/confidence but never assigns schema_version. ODDState.msg:3
  PUB /l3/m1/mode_cmd [PARTIAL_FIELDS] unpop:schema_version (stays default 0) :: odd_envelope_manager_node.cpp:1001-1014 publish_mode_cmd() sets stamp/mode/behavior_constraint/assumed_operator_state/confidence/rationale but never assigns schema_version. ModeCmd
  PUB /l3/sat/data [PARTIAL_FIELDS] unpop:sat1.active_alerts (hardcoded to empty list) :: odd_envelope_manager_node.cpp:1035-1036: sat1.state_summary set; sat1.active_alerts = {} hardcoded empty. SAT-1 operator display never receives active threat list regardless of ODD
  PUB /l3/safety/concern [PARTIAL_FIELDS] unpop:anchor_hdg (always 0.0F) :: odd_envelope_manager_node.cpp:832-838 M3 stale watchdog publishes SafetyConcernEvent with anchor_hdg hardcoded 0.0F; concern_type/severity/suggested_action are real values.
  SUB l3_external_msgs/CapabilityManifest (spec IDL upstream) [EXPECTED_MISSING] :: M1-spec.md:21 lists CapabilityManifest as upstream subscription. odd_envelope_manager_node.cpp:372-470 has NO CapabilityManifest create_subscription. 
  RESP[PARTIAL] Capability Manifest ROT_max dynamic read (no hardcode per Backseat Driver) :: ROT_max interpolated from rot_max_curve YAML (parameter_loader.cpp:171-182); no hardcoded constants. But M1-spec IDL requires a live DDS sub
  RESP[STUB] ODD zone transition (A/B/C/D) driven by external signals :: odd_envelope_manager_node.cpp:244 current_zone_=ODD_ZONE_A, never reassigned. grep -rn 'current_zone_' m1_odd_envelope_manager/ returns 3 hi
  RESP[PARTIAL] OUT_of_ODD → MRM via M7 path (MRM MUST go via Checker) :: M1 sets FSM=MrCPrep + publishes mode_cmd=MODE_EMERGENCY + tor_request when TDL<=TMR. But M1 does NOT publish a hard signal to M7 or /l3/safe
  RESP[PARTIAL] FMEDA M1 table v0.1 (≥20 failure modes) :: M1-FMEDA-v0.1.md exists with 11 failure modes. M1-spec.md:49 requires ≥20 and marks 🔴 未见. M1-progress D2.7 is 🔴 未启.
  RESP[PARTIAL] M7 VETO hard gate (independent Checker authority > Doer FSM) :: M7→M1 VETO uses SafetyAlert severity channel only (odd_envelope_manager_node.cpp:796-800). The X-axis /l3/checker/veto CheckerVetoNotificati
  DELTA[CRITICAL] current_zone_ frozen at ODD_ZONE_A — zone dimension of ADR-001 is dead letter
      D:ADR-001 and M1-spec: M1 is the single authority for ODD zone (A/B/C/D). Zone drives FSM threshold tightening (Zone C: edge_to_out ≥0.6) and 
      I:odd_envelope_manager_node.cpp:244 initializes current_zone_=ODD_ZONE_A. grep -rn 'current_zone_' in entire m1_odd_envelope_manager/ returns 
      ev:src/l3_tdl_kernel/m1_odd_envelope_manager/src/odd_envelope_manager_node.cpp:244 (init), :878 (FSM read), :929 (publish read). Zero assignmen
  DELTA[HIGH] ODDState and ModeCmd schema_version stays 0 — CMM contract broken
      D:ODDState.msg:3 and ModeCmd.msg:3 both declare 'uint16 schema_version # 121 = v1.1'. D2.1-report DoD#7: 'schema_version + confidence + ration
      I:on_odd_state_publish_tick() (line 926-970) and publish_mode_cmd() (line 1001-1014) never set schema_version. Only publish_tor_request() at l
      ev:grep -n 'schema_version' odd_envelope_manager_node.cpp → single hit at line 622 (ToRRequest). Confirmed no ODDState or ModeCmd schema_versio
  DELTA[HIGH] /l3/m1/tor_request has zero consumers — safety-critical ToR signal lost
      D:M1-spec IDL and D2.1-report §3 list /l3/m1/tor_request as downstream output to M8 for ROC/operator alerting. The computed TDL<=TMR violation
      I:grep -rn '/l3/m1/tor_request' across all src/ and docker/ excluding m1_odd_envelope_manager → 0 matches. M8 generates its own ToR on /l3/m8/
      ev:Bash: grep returns 0 consumers. M8 hmi_transparency_bridge_node.cpp:107-108 publishes /l3/m8/tor_request autonomously without reading M1.
  DELTA[MEDIUM] CapabilityManifest DDS subscription absent — runtime parameter update path missing
      D:M1-spec §IDL line 22: l3_external_msgs/CapabilityManifest (Parameter Database) listed as upstream subscription.
      I:odd_envelope_manager_node.cpp initialize_subscribers() has 11 subscriptions; none is CapabilityManifest. ROT_max loaded from YAML at startup
      ev:grep -rn 'CapabilityManifest' src/l3_tdl_kernel/m1_odd_envelope_manager/ → 0 results. parameter_loader.cpp:171-182.
  DELTA[MEDIUM] M7 VETO uses soft SafetyAlert channel only — hard X-axis VETO path absent
      D:M1-spec: 'M7 VETO 到达 → 立即 MrCPrep, 不经过自身 FSM'. Doer-Checker requires Checker authority to be hard and independent.
      I:M1 reads M7 only via /l3/m7/safety_alert SEVERITY_CRITICAL (odd_envelope_manager_node.cpp:796-800). The X-axis /l3/checker/veto CheckerVetoN
      ev:odd_envelope_manager_node.cpp: no CheckerVetoNotification subscription. safety_supervisor_node.cpp:174-177 sub_veto_ consumed M7-internal.
  DELTA[MEDIUM] MODE_LIMITED and MODE_DEGRADED from ModeCmd consumed nowhere — mode gate is MODE_EMERGENCY only
      D:ModeCmd defines MODE_NORMAL/DEGRADED/LIMITED/EMERGENCY and CONSTRAINT_SPEED/HEADING/BOTH. ADR-001: mode_cmd is the behavior switch authority
      I:M4 behavior_arbiter_node.cpp:140-143 maps mode_cmd only to mode_mrc_triggered=(mode==MODE_EMERGENCY). behavior_activation.cpp:61 triggers MR
      ev:grep -rn 'MODE_LIMITED|MODE_DEGRADED' src/l3_tdl_kernel/m4_behavior_arbiter/src/ → 0 results. grep -rn 'mode_cmd' M5/M6 src/ → 0 results.
  DELTA[LOW] sat1.active_alerts permanently empty — SAT-1 operator threat list suppressed
      D:CMM SAT-1 requires active ODD threat list surfaced to operator via M8 HMI for informed ToR decision.
      I:odd_envelope_manager_node.cpp:1036 hardcodes msg.sat1.active_alerts = {} on every 10Hz tick. No code path populates it.
      ev:odd_envelope_manager_node.cpp:1035-1036.
  MOCK[MASQ] current_zone_ frozen at ODD_ZONE_A — zone dimension of M1 masquerades as real @ src/l3_tdl_kernel/m1_odd_envelope_manager/src/odd_envelope_manager_node.cpp:244
  MOCK[MASQ] sat1.active_alerts hardcoded to empty list @ src/l3_tdl_kernel/m1_odd_envelope_manager/src/odd_envelope_manager_node.cpp:1036
  MOCK[stub] FMEDA v0.1 with 11 failure modes when spec requires ≥20 @ docs/Design/Safety/FMEDA/M1-FMEDA-v0.1.md:24
  OVERCLAIM: M1-progress.md D2.1 row: '✅ 2026-05-21 — zone/health-aware FSM + EMA + ToR adaptive matrix + Capabil || reality:D2.1-report.md status is '🟡 设计完成，验证待执行'. Four concrete gaps remain in current code: (1) schema_versi
  OVERCLAIM: M1-progress.md D2.1: 'FMEDA v0.1 (11 失效模式)' listed as part of ✅ completed D2.1 deliverable || reality:M1-spec.md:49 requires '≥ 20 失效模式' and marks it 🔴 未见. M1-progress D2.7 is explicitly '🔴 未启'. Deliver
  FLOWGAP? M1 → (nobody) via /l3/m1/tor_request M1 computes and publishes ToRRequest when TDL<=TMR — the safety-critical operator takeover trigger. Zero modul
  FLOWGAP? CapabilityManifest (Parameter Database) → M1 (specified upstream, never arrives) M1-spec declares CapabilityManifest as upstream input. No subscription exists. If Parameter Database updates v
  FLOWGAP? M1 ODD Zone signal → M2 CPA horizon (zone-dependent threshold dead-letter) world_state_aggregator.cpp:399-404 indexes cpa_safe_m[odd_zone_idx] for ENC horizon radius. Since M1 always pu
  FLOWGAP? X-axis CheckerVetoNotification → M1 (hard VETO channel absent) Doer-Checker architecture requires X-axis Checker to independently VETO the Doer. M7 receives /l3/checker/veto

### M2 — Sole authoritative world-view publisher for L3 Tactical Layer: fuses nav-filter + tracked-target + environment inputs, computes CPA/TCPA and COLREG ge
  PUB /l3/m2/world_state [PARTIAL_FIELDS] unpop:schema_version (stays 0 — never assigned),targets[].schema_version (stays 0),targets[].rationale (never assigned, stays empty string) :: world_state_aggregator.cpp:463-483 builds WorldState ws; ws.stamp, ws.targets, ws.own_ship, ws.zone, ws.confidence, ws.rationale are populated; ws.schema_version is never assigned 
  PUB /l3/m2/threat_state [MISSING] unpop:cpa_status,target_relative_position,confidence,rationale :: world_model_node.hpp:130-132 declares only world_state_pub_, sat_pub_, asdr_pub_ — no threat_state publisher. grep across entire src/ for 'l3/m2/threat_state' returns zero hits in 
  RESP[PARTIAL] ROS2 node + timer-driven publish :: world_model_node.cpp:32-41 — node exists, 4 timers set up. Timer fires on_aggregation_timer at configured aggregation_rate_hz=4.0 Hz. Spec §
  RESP[STUB] SOG validation f(Manifest.max_speed × 1.2) (MUST-6, claimed fixed in D0.1) :: test_must6_sog_validation.cpp:13-16 defines a file-local validate_sog() function that is NEVER called from world_model_node.cpp or world_sta
  RESP[MISSING] intent_distribution[] field (v3.0 spec, B P1-B-02 resolution) :: WorldState.msg has no intent_distribution field. TrackedTarget.msg has intent_confidence (scalar, D2.2 NEW), but the spec M2-spec.md line 26
  RESP[PARTIAL] Environment sanity check (visibility/Hs/current/staleness) :: EnvSanityChecker::validate() (env_sanity_checker.cpp:14-73) implements: (1) staleness check (age > staleness_max_s), (2) current_speed > 10 
  RESP[MISSING] /l3/m2/threat_state publication (ThreatState with cpa_status/target_relative_position :: No publisher for /l3/m2/threat_state exists anywhere in the M2 implementation. world_model_node.hpp:130-132 declares 3 publishers only. The 
  RESP[STUB] Target classification (fishing/passenger/cargo/tanker) :: world_state_aggregator.cpp:280-295 implements classification by SOG threshold. However parameter_loader.hpp:48 declares target_classificatio
  RESP[PARTIAL] CMM interface: current_state()/rationale()/forecast(Δt)+uncertainty() :: SAT data (sat1.state_summary, sat2.reasoning_chain, sat2.system_confidence) published via publish_sat_data() covers rationale() and current_
  RESP[STUB] OwnShipState.r_dot_deg_s (yaw rate) population :: world_state_aggregator.cpp:372 comment: 'r_dot_deg_s and nav_mode not available from OwnShipSnapshot — use defaults'; os_msg.r_dot_deg_s = 0
  DELTA[HIGH] schema_version never set on WorldState and TrackedTarget
      D:WorldState.msg and TrackedTarget.msg declare 'uint16 schema_version # 112 = v1.1.2' as CMM-mandated field per architecture §3 and §15.1.
      I:world_state_aggregator.cpp:463-483 builds WorldState ws and never assigns ws.schema_version. TrackedTarget wt (line 209) never has wt.schema
      ev:world_state_aggregator.cpp:463-483; WorldState.msg:3; TrackedTarget.msg:3; A4000 ground truth sample schema_version=0
  DELTA[HIGH] /l3/m2/threat_state topic published nowhere — bridge subscriber starved
      D:M2 spec lists /l3/m2/threat_state as an expected output (ThreatState.msg: cpa_status, target_relative_position, confidence, rationale). The 
      I:No publisher exists. world_model_node.hpp declares only world_state_pub_, sat_pub_, asdr_pub_. grep for 'l3/m2/threat_state' in all src/ C++
      ev:world_model_node.hpp:130-132; sil_topic_bridge.py:418-420, 867-877; ThreatState.msg:1-9
  DELTA[HIGH] MUST-6 SOG validation is test-only dead code — production node skips it
      D:M2-spec.md §关键字段: 'sog 校验 f(Manifest.max_speed × 1.2)'. M2-progress.md D0.1 claims MUST-6 closed. Architecture rule: no vessel constants — M
      I:validate_sog() exists only in test_must6_sog_validation.cpp:13-16 as a file-local anonymous-namespace function. update_own_ship() in world_s
      ev:world_state_aggregator.cpp:74-100; test_must6_sog_validation.cpp:1-44; world_model_node.cpp (no Manifest include)
  DELTA[MEDIUM] Aggregation rate 4 Hz vs spec 10-50 Hz
      D:M2-spec.md §时间尺度: '10–50 Hz'. Architecture module table: M2 '10–50 Hz'.
      I:m2_params.yaml line 3: aggregation_rate_hz=4.0. WorldState.msg comment says '4 Hz (M2 内部 2 Hz 输入聚合 + 1 次插值/外推)'. on_aggregation_timer() fire
      ev:m2_params.yaml:3; world_model_node.cpp:292; WorldState.msg:2
  DELTA[MEDIUM] intent_distribution[] array absent — only scalar intent_confidence present
      D:M2-spec.md §下游发布: 'WorldStateMsg @ 50Hz: 含 intent_distribution[] 字段（v3.0 修订，B P1-B-02 整改）'.
      I:WorldState.msg has no intent_distribution field. TrackedTarget.msg has intent_confidence (float32 scalar, D2.2 NEW). world_state_aggregator.
      ev:WorldState.msg (full file, no intent_distribution); TrackedTarget.msg:22-23; world_state_aggregator.cpp:342-355; M2-spec.md:26
  DELTA[MEDIUM] OwnShipState.r_dot_deg_s hardcoded zero — yaw rate not propagated
      D:OwnShipState is sourced from FilteredOwnShipState (15-state EKF per spec §上游订阅), which should include yaw rate for downstream M5 MPC initial
      I:world_state_aggregator.cpp:372: os_msg.r_dot_deg_s = 0.0 with comment 'not available from OwnShipSnapshot — use defaults'. OwnShipSnapshot s
      ev:world_state_aggregator.cpp:371-372; world_model_node.hpp:107 (OwnShipSnapshot has no r_dot field)
  DELTA[MEDIUM] EnvSanityChecker missing 4 of 7 mandated checks
      D:M2-spec.md §关键字段: '环境字段 sanity check（visibility/Hs/current 范围 + 跨源 + staleness）'. D2.2 progress claims env sanity 落地.
      I:env_sanity_checker.cpp implements staleness, current_speed, zone_transition only. The class header has an explicit TODO (lines 45-50) listin
      ev:env_sanity_checker.hpp:45-50; env_sanity_checker.cpp:14-73
  DELTA[MEDIUM] Bridge computes CPA/TCPA locally instead of consuming M2 output
      D:M2 is the single authority for CPA/TCPA. WorldState.targets[].cpa_m and tcpa_s are M2's computed outputs. The bridge should consume these fo
      I:sil_topic_bridge.py:775-832 _compute_dcpa_tcpa() reimplements CPA/TCPA from raw position/velocity of own ship and target vessel, with explic
      ev:sil_topic_bridge.py:789, 855; docker/sil_topic_bridge.py:108-112 CPA_SAFE_M=1000.0 hardcoded
  DELTA[LOW] target_classification_enabled defaults false in all deployments — classification branch dead
      D:world_state_aggregator.cpp:280-295 implements SOG-based classification (fishing/passenger/cargo/tanker).
      I:parameter_loader.hpp:48 declares default false. m2_params.yaml has no target_classification_enabled key. Parameter stays false at runtime. T
      ev:world_state_aggregator.cpp:280; parameter_loader.hpp:48; m2_params.yaml (no key present)
  MOCK[MASQ] validate_sog() — MUST-6 SOG validation @ src/l3_tdl_kernel/m2_world_model/test/test_must6_sog_validation.cpp:13
  MOCK[MASQ] target classification by SOG threshold (fishing/passenger/cargo/tanker) @ src/l3_tdl_kernel/m2_world_model/src/world_state_aggregator.cpp:280
  MOCK[stub] OwnShipState.r_dot_deg_s hardcoded to 0.0 @ src/l3_tdl_kernel/m2_world_model/src/world_state_aggregator.cpp:372
  MOCK[MASQ] os_msg.nav_mode hardcoded to 'OPTIMAL' @ src/l3_tdl_kernel/m2_world_model/src/world_state_aggregator.cpp:381
  MOCK[MASQ] EnvSanityChecker::validate() — only 3 of 7 checks implemented @ src/l3_tdl_kernel/m2_world_model/src/env_sanity_checker.cpp:14
  OVERCLAIM: D0.1 Closed: MUST-6 (sog 校验改读 Manifest) — M2-progress.md line 11 || reality:The validate_sog() function exists only in a test file's anonymous namespace and is never called fro
  OVERCLAIM: D2.2 Closed: 'intent_confidence 字段已落地（B P1-B-02 决议闭环）' — M2-progress.md line 21 || reality:Partially true: intent_confidence scalar (float32) is populated in TrackedTarget. But M2-spec.md §下游
  OVERCLAIM: D2.2 Closed: 'env sanity' falls under '~900 LOC C++ + 18 Python tests' — M2-progress.md line 14 || reality:EnvSanityChecker is real and called in production, but env_sanity_checker.hpp:45-50 explicitly docum
  OVERCLAIM: D1.3.2.3 Closed: 'CPA/TCPA 真发布到 /sil/cpa_tcpa（foxglove_bridge 消费端落地）' — M2-progress.md line 12 || reality:The bridge does NOT consume M2's cpa_m/tcpa_s from world_state. It reimplements CPA/TCPA locally (si
  FLOWGAP? /l3/m2/threat_state: M2 → bridge (sil_topic_bridge._on_threat_state) Bridge subscribes to this topic for latch-release condition 1. Since M2 never publishes it, the cpa_status='cl
  FLOWGAP? M2 world_state.targets[].cpa_m → bridge CPA release logic M2 computes authoritative CPA/TCPA per target (world_state_aggregator.cpp:212-235) and publishes it in world_s
  FLOWGAP? /l3/m1/odd_state → M2 (ODD zone for CPA threshold selection) If M1 has not yet published odd_state, odd_cache_ is empty and compose_world_state() falls back to OddZone::A 

### M3 — Voyage-level state machine: validates VoyageTask from L1, tracks L2 planned route, projects ETA, gates task_validity (4-condition), triggers RouteRepl
  PUB /l3/m3/route_replan_request [PARTIAL_FIELDS] unpop:exclusion_zones — RouteReplanRequest.msg:14 declares 'GeoPath[] exclusion_zones'; publish_replan_request() never populates it — always empty array (grep confirmed). RFC-006 locked this field's format but M3 never fills it :: mission_manager_node.cpp:256-258, publish_replan_request() lines 907-939; stamp/schema_version/reason/deadline_s/confidence/rationale all set
  PUB /l3/asdr/record [PARTIAL_FIELDS] unpop:schema_version — publish_asdr_record() never sets msg.schema_version; defaults to 0. Only MissionGoal (121) and RouteReplanRequest (120) have schema_version set :: mission_manager_node.cpp:260-262, publish_asdr_record() lines 942-951
  PUB /l3/m3/tor_request [PARTIAL_FIELDS] unpop:schema_version — M3's publish_tor_request() (line 953) never sets msg.schema_version; M1's version sets 121 (odd_envelope_manager_node.cpp:622). No subscriber found for /l3/m3/tor_request outside tests — M8 publishes its own ToR on /l3/m8/tor_request, M1 on /l3/m1/tor_request; the /l3/m3/tor_request topic is published but unsubscribed in production code :: mission_manager_node.cpp:264-267, publish_tor_request() lines 953-964; stamp/reason/deadline_s/target_level/confidence/rationale/context_summary/recommended_action all populated
  PUB /l3/m3/mission_state [MISSING] unpop: :: M3 node has no publisher for this topic. M1 subscribes (odd_envelope_manager_node.cpp:459) and uses it for MRC selection (water_depth_m at line 756-757, in_anchorage_zone at 761-76
  SUB /l3/m3/mission_state [EXPECTED_MISSING] :: M1 subscribes at odd_envelope_manager_node.cpp:459 (kTopicMissionState=/l3/m3/mission_state); MissionState.msg has water_depth_m/in_anchorage_zone/is_
  RESP[PARTIAL] ETA projection (EtaProjector) :: eta_projector_->project() called lines 801-806; returns eta_s fed to MissionGoal; BUT speed_recommend_kn (derived from ETA vs planned_eta) n
  RESP[PARTIAL] ODD-aware replan (M1 ODD_StateMsg → RouteReplanRequest) :: on_odd_state() line 561 calls check_and_trigger_replan(*msg, current_eta_s, 0.0) — planned_eta_s hardcoded to 0.0; MissionInfeasible trigger
  RESP[PARTIAL] ToR request on L1 watchdog TIMEOUT :: evaluate_l1_watchdog() lines 680-684 calls publish_tor_request; BUT l1_watchdog_bypass_=true (line 118, set from param default true at line 
  RESP[PARTIAL] L1WatchdogMonitor: VoyageTask dropout → confidence decay :: L1WatchdogMonitor instantiated and used; BUT l1_watchdog_bypass_=true by default (line 118) disables all timeout/warning transitions in eval
  RESP[STUB] ENC route validation (depth/forbidden-zone/COG) :: on_world_state() line 625: bool has_enc_check = true — hardcoded constant, no EncRouteValidator class exists (M3-gap-fix-plan.md Task A unim
  RESP[MISSING] speed_recommend_kn field in MissionGoal :: MissionGoal.msg line 6 declares the field; grep for 'speed_recommend' in mission_manager_node.cpp returns 0 matches; M3-gap-fix-plan.md Task
  RESP[MISSING] MissionState publication (/l3/m3/mission_state) for M1 MRC context :: No publisher in setup_publishers(). M1 subscribes at odd_envelope_manager_node.cpp:459. MissionState.msg (water_depth_m, in_anchorage_zone, 
  RESP[STUB] RouteReplanRequest.exclusion_zones population (RFC-006) :: RouteReplanRequest.msg:14 declares GeoPath[] exclusion_zones; publish_replan_request() line 907-939 never fills this field — always empty
  RESP[STUB] Confidence fields dynamically computed :: RouteReplanRequest.confidence hardcoded 1.0F (line 937); ToRRequest.confidence hardcoded 1.0F (line 960); ASDRRecord.schema_version=0 (never
  RESP[PARTIAL] Timeout parameters from YAML (voyage_task_s, planned_route_s, etc.) :: declare_parameters() only declares timeout.world_state_s (line 126). Four timeout.* params (voyage_task_s, planned_route_s, speed_profile_s,
  DELTA[HIGH] Missing /l3/m3/mission_state publisher — M1 MRC context starved
      D:M3 should publish /l3/m3/mission_state (l3_msgs/MissionState) with water_depth_m / in_anchorage_zone / is_moored for M1 MRC selection. Missi
      I:M3 setup_publishers() (lines 250-267) has no publisher for /l3/m3/mission_state. M1 subscribes at odd_envelope_manager_node.cpp:459 and uses
      ev:grep confirmed: only one match for kTopicMissionState in all non-archive src — M1's subscriber at odd_envelope_manager_node.cpp:101,459. M3 
  DELTA[HIGH] speed_recommend_kn always 0 in MissionGoal — M4/M5 speed guidance missing
      D:MissionGoal.msg field speed_recommend_kn (line 6) should carry ETA-derived speed recommendation to M4/M5. M3-spec states ETA projection is a
      I:publish_mission_goal() never assigns speed_recommend_kn; defaults to 0.0F. No call to any speed recommendation function exists (grep returns
      ev:MissionGoal.msg:6; mission_manager_node.cpp lines 729-851 (full publish_mission_goal body); M3-gap-fix-plan.md Task B still has all checkbox
  DELTA[HIGH] has_enc_check hardcoded true — 4-condition validity gate degraded to 3 conditions
      D:update_task_validity(has_l1_task, has_l2_route, has_enc_check, autonomy_ok) — all 4 conditions must be true for TaskValidity::Valid. ENC che
      I:on_world_state() line 625: bool has_enc_check = true; — unconditionally true. No EncRouteValidator class exists; files enc_route_validator.h
      ev:mission_manager_node.cpp:625; codegraph_files shows no enc_route_validator.* files in m3_mission_manager/; M3-gap-fix-plan.md Task A all ste
  DELTA[MEDIUM] planned_eta_s=0.0 passed to check_and_trigger_replan — MissionInfeasible trigger broken
      D:check_and_trigger_replan takes current_eta_s and planned_eta_s; ReplanRequestTrigger evaluates ETA infeasibility by comparing them with an i
      I:on_odd_state() line 561: check_and_trigger_replan(*msg, current_eta_s, 0.0) — planned_eta_s is hardcoded 0.0. No planned_eta_s_ member exist
      ev:mission_manager_node.cpp:561; MissionManagerNode header has no planned_eta_s_ member
  DELTA[MEDIUM] l1_watchdog_bypass_ defaults true — L1 dropout ToR chain disabled
      D:L1WatchdogMonitor should detect VoyageTask dropout and trigger confidence decay (WARNING) → ToR request (TIMEOUT). M3-progress.md claims D2.
      I:declare_parameter('l1_watchdog.bypass', true) at line 54; l1_watchdog_bypass_ set from this at line 64. evaluate_l1_watchdog() returns immed
      ev:mission_manager_node.cpp:54,64,118,654-660
  DELTA[MEDIUM] /l3/m3/tor_request has no production subscriber — ToR from M3 is a dead-end
      D:M3 should escalate L1 dropout via ToR to the ROC/HMI chain. The spec defines ToR as a cross-module escalation path.
      I:M3 publishes on /l3/m3/tor_request (line 265). M8 publishes its own ToR on /l3/m8/tor_request (hmi_transparency_bridge_node.cpp:108). M1 pub
      ev:grep -rn 'l3/m3/tor_request' src: only mission_manager_node.cpp:265 (publisher) and test_m3_dual_subscription.cpp:81 (test subscriber)
  DELTA[LOW] ASDRRecord.schema_version never set — audit trail version field always 0
      D:Architecture mandates schema_version on all inter-module messages for traceability. ASDRRecord is the primary audit/ASDR trail.
      I:publish_asdr_record() (lines 942-951) creates ASDRRecord without setting schema_version — defaults to 0. All M3 ASDR events (voyage_task_acc
      ev:mission_manager_node.cpp:942-951; MissionGoal sets 121, RouteReplanRequest sets 120, but ASDRRecord omitted
  DELTA[LOW] RouteReplanRequest.exclusion_zones always empty — L2 replan lacks spatial context
      D:RouteReplanRequest.msg line 14 declares GeoPath[] exclusion_zones (RFC-006 locked format) to give L2 the no-go geometry for replanning.
      I:publish_replan_request() lines 907-939 never populates msg.exclusion_zones. L2 (mock_l2_publisher.py) receives requests with empty exclusion
      ev:mission_manager_node.cpp:907-939; RouteReplanRequest.msg:14
  MOCK[MASQ] has_enc_check = true (ENC route validation stub) @ src/l3_tdl_kernel/m3_mission_manager/src/mission_manager_node.cpp:625
  MOCK[MASQ] speed_recommend_kn = 0 (zero-default, never assigned) @ src/l3_tdl_kernel/l3_msgs/msg/MissionGoal.msg:6 / mission_manager_node.cpp:729-851
  MOCK[stub] l1_watchdog_bypass_ = true (watchdog disabled by default) @ src/l3_tdl_kernel/m3_mission_manager/src/mission_manager_node.cpp:54,64,118
  MOCK[MASQ] planned_eta_s = 0.0 passed to check_and_trigger_replan (no planned ETA tracking) @ src/l3_tdl_kernel/m3_mission_manager/src/mission_manager_node.cpp:561
  MOCK[stub] Missing /l3/m3/mission_state publisher (entire topic absent) @ src/l3_tdl_kernel/m3_mission_manager/src/mission_manager_node.cpp:250-267
  OVERCLAIM: D2.3 closed 2026-05-21: IDL v1.2.0 with schema_version=120 + 4 new fields || reality:Current code uses schema_version=121 (IDL v1.2.1) for MissionGoal (lines 737,766). schema_version=12
  OVERCLAIM: D2.3 closed: Closes F P1-F-01 (L1/L2 independence: tested IT-01~IT-06) || reality:l1_watchdog_bypass_=true is the default at startup (declare_parameter line 54, default=true). All IT
  OVERCLAIM: D1.4 closed 2026-05-20, D2.3 closed 2026-05-21 — M3 implementation complete || reality:7 open GAPs documented in M3-gap-fix-plan.md (created 2026-06-08): GAP-1 ENC validation missing, GAP
  FLOWGAP? M3 → M1 via /l3/m3/mission_state M1 subscribes (odd_envelope_manager_node.cpp:459) and uses water_depth_m / in_anchorage_zone / is_moored for M
  FLOWGAP? M3 → L2 via /l3/m3/route_replan_request (exclusion_zones empty) L2 mock_l2_publisher._on_replan_request() receives a replan request without spatial no-go zones. Any L2 replan
  FLOWGAP? /l3/m3/tor_request → no subscriber M3 publishes L1-dropout ToR on /l3/m3/tor_request but no production module subscribes. The ToR notification ne
  FLOWGAP? on_odd_state → check_and_trigger_replan with planned_eta_s=0.0 Every ODD-state message potentially triggers MISSION_INFEASIBLE replan (current_eta_s > 0 + infeasible_margin_

### M4 — IvP multi-objective behavior arbitration: consumes M1/M2/M3/M6 inputs, selects the dominant behavior from a dictionary, runs a weighted-sum grid-searc
  PUB /sil/sat2_data [PARTIAL_FIELDS] unpop:ivp_contributions[6] truncates 8 computed directions to 6 (270deg and 315deg dropped),reasoning_latency_ms :: behavior_arbiter_node.cpp:551-576 — schema_version=113, stamp, confidence, rationale, trigger_reason, reasoning_chain, system_confidence, colregs_chain all set. SAT2Data.msg:17 dec
  PUB /l3/safety/concern [PARTIAL_FIELDS] unpop: :: behavior_arbiter_node.cpp:487-494 — published only on fallback_anchor latch; concern_type, anchor_hdg, suggested_action, severity, stamp set. Not listed in M4-spec.md interface con
  PUB /l3/m4/reactive_override_cmd [MISSING] unpop: :: M4-spec.md:26 lists '/l3/m4/reactive_override_cmd' as DEMO-2 P0 NEW publication. No publisher for this topic in src/l3_tdl_kernel/m4_behavior_arbiter/src/ or include/. BehaviorArbi
  RESP[PARTIAL] 5-behavior weight table per spec (Transit 0.3, COLREGs_Avoidance 0.7, Restricted_Vis  :: m4_params.yaml:11-26 defines weights correctly. However arbitration_timer_callback() hardcodes TRANSIT weight=1.0 (line 298) and COLREG_AVOI
  RESP[PARTIAL] ODD-aware behavior activation (behavior enabled/disabled per ODD zone) :: behavior_activation.cpp:26-57 — is_transit_applicable checks odd_zone<=2, is_colreg_avoid_applicable checks odd_zone!=2, is_restricted_vis_a
  RESP[MISSING] M6 COLREGs primary_preferred_direction drives turn direction selection :: COLREGsConstraint.msg:8 defines primary_preferred_direction (STARBOARD|PORT|REDUCE_SPEED|HOLD). Zero references in behavior_arbiter_node.cpp
  RESP[PARTIAL] SAT-2 ivp_contributions 8-direction IvP risk gradient at 4Hz :: behavior_arbiter_node.cpp:566-575 — compute_ivp_contributions returns 8 directions (kDirections[8] at line 616) but SAT2Data.msg:17 is float
  RESP[MISSING] CMM 3-interface: current_state() / rationale() / forecast(delta_t)+uncertainty() :: behavior_arbiter_node.hpp — full class definition shows no ROS2 service server members. No create_service() calls in behavior_arbiter_node.c
  RESP[PARTIAL] M1 ODD as sole authority for behavior switching (ADR-1) :: behavior_activation.cpp uses ODD zone as primary switch for all behavior predicates. However on_rule_assessment():115-122 directly mutates d
  RESP[MISSING] Publish /l3/m4/reactive_override_cmd (DEMO-2 P0 per spec) :: M4-spec.md:26 lists reactive_override_cmd. No pub_reactive_override_ publisher in BehaviorArbiterNode constructor (lines 69-73). Topic name 
  RESP[STUB] Restricted_Visibility behavior (ODD zone 3, visibility < 2nm) :: behavior_activation.cpp:42-48 — is_restricted_vis_applicable() exists but world_visibility_nm always 999.0 (build_inputs():136). Condition i
  RESP[STUB] Channel_Follow / BERTH behavior (ODD zone 1 VTS zone) :: behavior_activation.cpp:51-56 — is_channel_follow_applicable() checks world_in_vts_zone which is always false (never set in build_inputs(); 
  DELTA[HIGH] primary_preferred_direction from M6 ignored — starboard hardcoded for all encounters
      D:COLREGsConstraint.msg:8 defines primary_preferred_direction as STARBOARD|PORT|REDUCE_SPEED|HOLD. M4 is expected to consume M6 directional gu
      I:behavior_arbiter_node.cpp:301-398 never reads primary_preferred_direction. IvP avoid function always constructs a starboard-biased window: p
      ev:COLREGsConstraint.msg:8 — field exists and is populated by M6; grep of behavior_arbiter_node.cpp for 'primary_preferred_direction' returns z
  DELTA[HIGH] /l3/m4/reactive_override_cmd publication missing — DEMO-2 P0 item absent
      D:M4-spec.md:26 lists '/l3/m4/reactive_override_cmd' as DEMO-2 P0 NEW publication downstream to M5.
      I:No publisher for this topic anywhere in current m4_behavior_arbiter/src/ or include/. BehaviorArbiterNode constructor (lines 69-73) creates 
      ev:behavior_arbiter_node.cpp:69-73; grep of src/ for 'reactive_override' returns zero matches in non-salvage code
  DELTA[MEDIUM] IvP weighted_fns use hardcoded weights (1.0 TRANSIT, 10.0 COLREG_AVOID) ignoring YAML behavior_weights
      D:M4-spec.md behavior dictionary: Transit=0.3, COLREGs_Avoidance=0.7. m4_params.yaml:11-17 configures these weights. IvP combination should re
      I:behavior_arbiter_node.cpp:298 — weighted_fns.push_back({1.0, transit_fn}); line 398 — weighted_fns.push_back({10.0, avoid_fn}). Magic number
      ev:behavior_arbiter_node.cpp:298, 398; m4_params.yaml:11-17; on_rule_assessment():117,121 mutates dictionary_ but weighted_fns construction nev
  DELTA[MEDIUM] Restricted_Visibility behavior permanently disabled — world_visibility_nm removed from WorldState
      D:M4-spec.md behavior dictionary: Restricted_Vis weight=0.6, activated when visibility < 2nm in ODD zone 3.
      I:build_inputs():136 — comment states 'visibility_nm removed from WorldState (v1.1.2); keep default 999.0'. ArbitrationInputs::world_visibilit
      ev:behavior_arbiter_node.cpp:136; behavior_activation.cpp:42-49
  DELTA[MEDIUM] compute_active_set maps Restricted_Visibility trigger to DP_HOLD BehaviorType (wrong enum)
      D:Spec lists Restricted_Visibility as a distinct behavior with weight 0.6 and different IvP semantics from DP_Hold (weight 0.8).
      I:behavior_activation.cpp:84-85 — if (is_restricted_vis_applicable(inputs)) { active.push_back(BehaviorType::DP_HOLD); }. The restricted visib
      ev:behavior_activation.cpp:84-85
  DELTA[MEDIUM] CMM 3-interface (current_state/rationale/forecast) absent from M4
      D:Architecture ADR-3: every module implements CMM current_state() / rationale() / forecast(delta_t)+uncertainty(), queryable by M8.
      I:BehaviorArbiterNode has no ROS2 service servers and no methods named current_state or forecast. No create_service() calls in behavior_arbite
      ev:behavior_arbiter_node.hpp — full class definition shows no service server members; behavior_arbiter_node.cpp has no create_service() calls
  DELTA[LOW] SAT2Data ivp_contributions silently truncated from 8 computed directions to 6 published slots
      D:M4-spec.md:26 and M4-progress.md:18 reference 8-direction IvP risk gradient for IvpRiskGradientLayer frontend.
      I:compute_ivp_contributions() at line 616 defines kDirections[8]={0,45,90,135,180,225,270,315}. SAT2Data.msg:17 declares float32[6]. Loop at l
      ev:behavior_arbiter_node.cpp:566-574; SAT2Data.msg:17; compute_ivp_contributions():616
  DELTA[LOW] M4 directly couples to M6 rule_assessment to mutate behavior weights — bypasses M1 ODD authority (ADR-1 violation)
      D:ADR-1: M1 ODD state is the sole source for behavior switching. Modules must not independently maintain safety context.
      I:on_rule_assessment():113-123 — on receiving '/l3/m6/rule_assessment', M4 directly sets colreg_avoidance_weight_ and calls dictionary_.set_pr
      ev:behavior_arbiter_node.cpp:113-123; M4-spec.md:19-23 subscription list omits /l3/m6/rule_assessment
  MOCK[MASQ] Restricted_Visibility behavior — dead code, world_visibility_nm hardcoded 999.0 @ src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_arbiter_node.cpp:136 and behavior_activation.cpp:42-48
  MOCK[MASQ] Channel_Follow / BERTH behavior — world_in_vts_zone never set, no IvP function built @ src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_arbiter_node.cpp build_inputs() and behavior_activation.cpp:51-56
  MOCK[MASQ] dictionary_.set_priority_weight() called from on_rule_assessment() but weights never read in IvP function construction @ src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_arbiter_node.cpp:117,121 vs 298,398
  MOCK[stub] IvP infeasible fallback emitting +/-90 degree symmetric window when no COLREGs deviation @ src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_arbiter_node.cpp:513-516
  OVERCLAIM: M4-progress.md:13 — D3.1 'Closed in 2026-05-25: M4 BehaviorArbiter IvP complete implementation (813  || reality:IvP solver is real and grid search works. However: (a) primary_preferred_direction from M6 never con
  OVERCLAIM: M4-progress.md:14 — D2.5 'M4 side has published /sil/sat2_data.ivp_contributions[] @4Hz' || reality:Publication exists and fires at 4Hz. However ivp_contributions is float32[6] in SAT2Data.msg but 8 d
  OVERCLAIM: M4-spec.md:53 — '/sil/sat2_data.ivp_contributions[] publication' listed as 'not done' (red) in spec  || reality:The spec's own status table (line 53) was never updated after D3.1 merge and still shows the item as
  FLOWGAP? M6 primary_preferred_direction to M4 avoidance direction selection M4 always builds a starboard avoidance window regardless of what M6 specifies in primary_preferred_direction. 
  FLOWGAP? M7 safety supervisor VETO to M4 behavior plan gate M4 has no subscription to any M7 veto or safety state topic. No mechanism to halt or override pub_plan_ output
  FLOWGAP? /l3/m6/rule_assessment to M4 (undeclared upstream dependency) M4 subscribes to /l3/m6/rule_assessment which is not declared in M4-spec.md interface contract. Any M6 schema 

### M5 — Mid-MPC (IPOPT/CasADi, 1 Hz) + BC-MPC (event-driven, 4-10 Hz) — computes avoidance trajectory and short-horizon reactive override. Publishes /l3/m5/av
  PUB /m5/avoidance_plan [PARTIAL_FIELDS] unpop:waypoints[*].schema_version (NLP success path only),waypoints[*].stamp (all paths),waypoints[*].confidence (NLP success path only),waypoints[*].rationale (NLP success path only),cost_colreg / cost_dist / cost_vel (always zero — unpack_solution comment: 'Phase E1: cost breakdowns not split out', mid_mpc_nlp_formulation.cpp:296-298) :: mid_mpc_node.cpp:79 publishes on /m5/avoidance_plan. Bridge consumes /l3/m5/avoidance_plan (sil_topic_bridge.py:372). Topic mismatch — mid_mpc_node.cpp:79 has no /l3/ prefix, no re
  PUB /m5/sat_data [PARTIAL_FIELDS] unpop: :: mid_mpc_node.cpp:411-417 — stamp, source_module, sat2.trigger_reason, sat2.reasoning_chain, sat2.system_confidence populated. SAT-3 fields not populated here (they go to /sil/sat3_
  PUB /sil/sat3_data [PARTIAL_FIELDS] unpop:tc.rule_compliant (always false — not evaluated),tc.confidence (always 1.0F — not derived from solver quality) :: mid_mpc_node.cpp:428-456 — stamp and schema_version=112 set on SAT3Data. Each TrajectoryCandidate: tc.confidence=1.0F (hardcoded), tc.rule_compliant=false (hardcoded always, mid_mp
  PUB /m5/reactive_override_cmd [STUB] unpop: :: bc_mpc_node.cpp:46-47, publish_override_ at bc_mpc_node.cpp:164-181 — BC-MPC node exists and compiles; publishes on /m5/reactive_override_cmd. However: (1) BC-MPC is NOT in m5_mid_
  PUB /m5/asdr_record_bc [STUB] unpop: :: bc_mpc_node.cpp:48-49 — declared but BC-MPC node is never launched; output never produced.
  SUB /m2/world_state [DECLARED_UNUSED] :: bc_mpc_node.cpp:34-37 — BC-MPC subscribes to /m2/world_state (no leading /l3/), a different namespace from what M2 publishes (/l3/m2/world_state). No 
  SUB /m5/avoidance_plan [DECLARED_UNUSED] :: bc_mpc_node.cpp:40-43 — BC-MPC subscribes to /m5/avoidance_plan (no /l3/ prefix); Mid-MPC publishes on /m5/avoidance_plan (mid_mpc_node.cpp:79). Names
  SUB /l3/m1/odd_state [EXPECTED_MISSING] :: mid_mpc_node.hpp (full file) and mid_mpc_node.cpp (full constructor) — no subscription to M1 ODD state. M5 does not gate on ODD state at all. ADR-1 (O
  SUB /l3/checker/veto [EXPECTED_MISSING] :: mid_mpc_node.cpp and bc_mpc_node.cpp — neither node subscribes to any checker veto topic. M5 has no hard-gate on X-axis VETO.
  RESP[PARTIAL] Mid-MPC NLP (IPOPT/CasADi) N=18 / 90s horizon avoidance trajectory at 1-2 Hz :: mid_mpc_node.cpp:87-92 — timer at 1 Hz (std::chrono::seconds(1)). NLP formulation default n_horizon=12 (mid_mpc_nlp_formulation.hpp:67); m5_
  RESP[STUB] BC-MPC 13-candidate short-horizon override at 4-10 Hz (SLA < 150ms) :: bc_mpc_node.cpp exists with real solver calls but: node not launched (not in m5_mid_mpc.launch.py), not in docker-compose, subscribes to wro
  RESP[MISSING] Gate on M1 ODD state (ADR-1: ODD = unique authority) :: mid_mpc_node.cpp constructor (lines 34-93) — no subscription to /l3/m1/odd_state or any ODD topic. M5 runs regardless of ODD envelope status
  RESP[MISSING] Gate on X-axis Checker VETO (ADR-1: VETO authority) :: mid_mpc_node.cpp and bc_mpc_node.cpp — no subscription to checker veto notification. M5 does not honor X-axis VETO.
  RESP[PARTIAL] DEMO-2 P0: /sil/sat3_data trajectory_candidates at 2 Hz (Nomoto fallback) :: mid_mpc_node.cpp:423-457 — pub_sat3_data_ publishes on /sil/sat3_data every solve cycle (1 Hz, not 2 Hz). Nomoto fallback solve called; tc.r
  RESP[MISSING] urgency_level > 0.95 triggers BC-MPC ±60° expansion (P2-B-01) :: bc_mpc_node.cpp — BC-MPC not launched; urgency_level field not read anywhere in mid_mpc_node.cpp or bc_mpc_node.cpp.
  RESP[PARTIAL] MRM walks M7 path (MUST-9): M5 escalates to M7 on consecutive failures :: mid_mpc_solver.cpp:169-170, 207-210 — logs spdlog::critical when consecutive_failures_ > 5. But 'escalation' is only a log message; no ROS2 
  RESP[PARTIAL] CMM three-interface: current_state(), rationale(), forecast(Δt)+uncertainty() via sch :: Plan-level fields populated (stamp, status, rationale, confidence). Waypoint-level fields unpopulated in NLP success path (schema_version=0,
  DELTA[CRITICAL] BC-MPC node never launched — entire short-horizon layer is dead in production
      D:Spec §1: BC-MPC runs at 4-10 Hz, SLA <150ms, with 13 candidate branches. It is the short-horizon safety layer complementing Mid-MPC.
      I:BC-MPC node exists in src/ and compiles, but m5_mid_mpc.launch.py only launches m5_mid_mpc_node (mid_mpc). There is no BC-MPC entry in docke
      ev:m5_mid_mpc.launch.py:11-21 (only m5_mid_mpc_node launched); docker/sil_topic_bridge.py (zero matches for reactive_override_cmd or bc_mpc); s
  DELTA[HIGH] BC-MPC subscribes to wrong world_state namespace (/m2/ instead of /l3/m2/)
      D:M5 consumes M2 WorldState. In the running system M2 publishes on /l3/m2/world_state (A4000 ground truth: 20 Hz).
      I:bc_mpc_node.cpp:35 subscribes to /m2/world_state (no /l3/ prefix). No remapping in any launch or compose file. Even if BC-MPC were launched,
      ev:bc_mpc_node.cpp:35 vs mid_mpc_node.cpp:50 (/l3/m2/world_state)
  DELTA[HIGH] AvoidanceWaypoint CMM fields (schema_version, stamp, confidence, rationale) unpopulated in NLP success path
      D:Architecture report ADR: every message must carry stamp, schema_version, confidence∈[0,1], rationale. Live A4000 observation: per-waypoint s
      I:mid_mpc_waypoint_generator.cpp:87-129 (build_waypoints_) never sets wp.schema_version, wp.stamp, wp.confidence, or wp.rationale. These field
      ev:mid_mpc_waypoint_generator.cpp:87-129 (no schema_version/stamp/confidence/rationale assignments); mid_mpc_node.cpp:354-367 (fallback sets al
  DELTA[HIGH] cost_colreg / cost_dist / cost_vel always zero — COLREGs cost term invisible in rationale
      D:Spec: avoidance_plan carries IPOPT KKT residual and cost breakdown. Rationale should reflect COLREGs cost weighting.
      I:mid_mpc_nlp_formulation.cpp:296-298 comment: 'Phase E1: cost_total / cost_colreg / cost_dist / cost_vel are not split out from CasADi stats.
      ev:mid_mpc_nlp_formulation.cpp:296-298; mid_mpc_waypoint_generator.cpp:139
  DELTA[HIGH] M5 has no ODD state subscription — ADR-1 (ODD = unique authority) violated
      D:ADR-1: M1 ODD state is the unique switching authority. No module may run avoidance logic outside ODD envelope without M1 gating.
      I:mid_mpc_node.cpp constructor (lines 34-93): no subscription to /l3/m1/odd_state or any ODD topic. M5 runs its full solve cycle regardless of
      ev:mid_mpc_node.cpp:34-93 (complete constructor — no ODDState subscription); mid_mpc_node.hpp (no sub_odd_ member)
  DELTA[MEDIUM] Mid-MPC publishes avoidance_plan on /m5/ namespace but bridge consumes /l3/m5/ — resolved in run configuration, but launch file incomplete
      D:Bridge subscribes /l3/m5/avoidance_plan (sil_topic_bridge.py:372). Spec says publish to /l3/m5/avoidance_plan.
      I:mid_mpc_node.cpp:79 creates publisher on /m5/avoidance_plan (no /l3/ prefix). m5_mid_mpc.launch.py only remaps /m6/colregs_constraint, NOT a
      ev:mid_mpc_node.cpp:79 vs sil_topic_bridge.py:372; m5_mid_mpc.launch.py:16-18 (only one remapping)
  DELTA[MEDIUM] M7 MRM escalation is log-only — MUST-9 not wired
      D:Spec MUST-9: MRM walks M7 path. M5 must escalate to M7 on persistent NLP failure (FM-2 / consecutive failures).
      I:mid_mpc_solver.cpp:169-170, 207-210 — emits spdlog::critical log only. No publication to any M7 topic. No ROS2 service call. MRM-02 escalati
      ev:mid_mpc_solver.cpp:169-170, 207-210
  DELTA[MEDIUM] Mid-MPC horizon N=18 from params likely not applied — n_horizon defaults to 12
      D:Spec / RFC-001 LOCKED: Mid-MPC N=18 / 90s (N=18, dt=5s).
      I:mid_mpc_nlp_formulation.hpp:67 sets n_horizon default=12. main_mid_mpc.cpp passes MidMpcNode::Config{} (default). mid_mpc_node.cpp construct
      ev:mid_mpc_nlp_formulation.hpp:67 (n_horizon{12}); mid_mpc_node.cpp:47 (only declare_parameter); m5_params.yaml:5 (n_steps: 18 unused)
  DELTA[LOW] /sil/sat3_data trajectory_candidates: rule_compliant always false, confidence always 1.0F
      D:SAT-3 trajectory view should indicate which branches are COLREGs-compliant.
      I:mid_mpc_node.cpp:441 tc.rule_compliant = false (hardcoded); tc.confidence = 1.0F (hardcoded regardless of branch quality or COLREGs evaluati
      ev:mid_mpc_node.cpp:437, 441
  MOCK[MASQ] BC-MPC node (BcMpcNode) — entire short-horizon layer @ src/l3_tdl_kernel/m5_tactical_planner/src/bc_mpc/bc_mpc_node.cpp:1 / src/l3_tdl_kernel/m5_tactical_planner/src/bc_mpc/main_bc_mpc.cpp:1
  MOCK[MASQ] M5 MRM-02 escalation — spdlog::critical log treated as real MRM dispatch @ src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_solver.cpp:169-170, 207-210
  MOCK[stub] cost_colreg / cost_dist / cost_vel fields in MidMpcSolution @ src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_nlp_formulation.cpp:296-298
  MOCK[stub] Sea-state input to VesselDynamicsModel (hs_m hardcoded to 0.0) @ src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp:208
  MOCK[stub] M4 consecutive-failure counter in BC-MPC (mid_mpc_consecutive_failures hardcoded 0) @ src/l3_tdl_kernel/m5_tactical_planner/src/bc_mpc/bc_mpc_node.cpp:155
  MOCK[stub] [M5DIAG] debug residual logging in mid_mpc_solver.cpp — marked temporary @ src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_solver.cpp:24-65
  OVERCLAIM: D3.2 Closed in ✅ 2026-05-25: 'M5 双 MPC 完整实装（2751 LOC src + 2218 LOC test）' — dual MPC fully implemen || reality:BC-MPC (the second MPC) exists as compilable code but is not launched, not wired to the bridge, subs
  OVERCLAIM: DEMO-2 阻塞 ✅ 已解除: '/sil/sat3_data.trajectory_candidates[] @2Hz 已实装' || reality:sat3_data IS published but at 1 Hz (solve_timer_ = std::chrono::seconds(1), mid_mpc_node.cpp:88-92),
  OVERCLAIM: D0.1 MUST-9 Surgical fix ✅ Closed: 'MRM 走 M7 路径' || reality:MRM escalation is a spdlog::critical() log call only. No ROS2 message to M7 MRM topic is published o
  FLOWGAP? BC-MPC → L4 Guidance via /m5/reactive_override_cmd BC-MPC node never started; /m5/reactive_override_cmd has zero publishers in the running system. The entire sho
  FLOWGAP? M5 consecutive NLP failures → M7 MRM-02 trigger After 5 consecutive IPOPT failures, M5 logs spdlog::critical but publishes nothing. M7 has no way to detect th
  FLOWGAP? M1 ODD state → M5 solve inhibit M5 runs its full IPOPT solve cycle unconditionally. If ODD transitions to OUT-of-ODD or CRITICAL, M5 continues
  FLOWGAP? X-axis Checker VETO → M5 output suppression No subscription to checker veto in either Mid-MPC or BC-MPC node. A VETO from the X-axis Deterministic Checker

### M6 — Rule reasoning (ODD-aware), encounter classification, 5-layer decision chain, COLREGsConstraint publication for M4/M5 consumption. Runs at 2 Hz (defau
  PUB /l3/m6/colregs_constraint [PARTIAL_FIELDS] unpop:schema_version :: colregs_reasoner_node.cpp:569-576 sets stamp, phase, confidence, rationale, colregs_chain — but schema_version (declared as 114 in COLREGsConstraint.msg) is never assigned anywhere
  PUB /l3/m6/rule_assessment [PARTIAL_FIELDS] unpop:only fires for Rule14 latch — Rule13/15 latched encounters never emit assessment :: colregs_reasoner_node.cpp:594-609 — only kKey = mmsi<<8|14ULL checked; Rule13 and Rule15 latches exist but no assessment published for those rule IDs
  PUB /l3/sat/data [PARTIAL_FIELDS] unpop:schema_version,sat2.reasoning_chain (hardcoded empty string),sat2.colregs_chain (never populated),sat2.colregs_chain_target_id,sat2.reasoning_latency_ms,sat3.tmr_s (hardcoded 60.0 not ODD-aware) :: colregs_reasoner_node.cpp:710-729: msg.sat2.reasoning_chain=''; sat2.colregs_chain array never assigned; sat3.predicted_state='nominal' hardcoded; schema_version never set
  RESP[PARTIAL] Rule 5-19 reasoning engine :: Rules 5,6,7,8,13,14,15,16,17,18,19 all have evaluate() implementations. Rule16_GiveWay fires on cpa<1.5*safe regardless of whether Rule13/14
  RESP[PARTIAL] 5-layer decision chain internal generation :: build_colregs_chain() at colregs_reasoner_node.cpp:842-966 builds all 5 layers. Chain is populated into COLREGsConstraint.colregs_chain (lin
  RESP[STUB] SAT-2 colregs_chain[5] serialization output (DEMO-2 P0) :: colregs_reasoner_node.cpp:717-719: sat2.reasoning_chain=''; sat2.colregs_chain never assigned. SAT2Data.msg lines 12-13 declare the fields. 
  RESP[PARTIAL] ODD-aware rule set selection (ADR-1) :: load_odd_thresholds() at line 248 loads per-ODD params (t_standOn_s, t_act_s, t_emergency_s, cpa_safe_m). These flow into evaluate() via Rul
  RESP[PARTIAL] CMM three-interface: current_state / rationale / forecast :: current_state approximated via sat1.state_summary string (line 714); rationale carried in constraint.rationale (line 111). sat3.predicted_st
  DELTA[HIGH] SAT2 colregs_chain never populated — DEMO-2 P0 gap
      D:M6-spec.md line 25: 'DEMO-2 P0 NEW: /sil/sat2_data.colregs_chain[5] 序列化'; SAT2Data.msg lines 12-13 declares fields
      I:publish_sat_data() lines 700-729 only sets trigger_reason, reasoning_chain='', system_confidence — colregs_chain array is left empty on ever
      ev:colregs_reasoner_node.cpp:717-719 — no sat2.colregs_chain assignment; build_colregs_chain() result wired only to COLREGsConstraint (line 573
  DELTA[MEDIUM] schema_version never set on /l3/m6/colregs_constraint
      D:COLREGsConstraint.msg declares schema_version=114 as mandatory versioning field (per v1.1.3-stub §15.1)
      I:constraint_generator.cpp and colregs_reasoner_node.cpp never assign constraint.schema_version; every published message carries schema_versio
      ev:colregs_constraint_generator.cpp:43 — msg is default-constructed, schema_version never assigned; colregs_reasoner_node.cpp:569-576 — no sche
  DELTA[MEDIUM] Rule16 standalone fires without latch — chatters during latched Rule14/15 encounters
      D:Rule 16 action obligation derives from Rule13/14/15 role assignment; once encounter is latched per Rule13(d), classification should be stabl
      I:Rule16_GiveWay.evaluate() (rule16_give_way.cpp:26) fires independently on cpa<1.5*cpa_safe regardless of latch state; latch block at colregs
      ev:colregs_reasoner_node.cpp:530: 'if (rid == 14 || rid == 15)'; rule16_give_way.cpp:26: unconditional CPA check
  DELTA[MEDIUM] M5 does not consume M6 heading direction — uses only binary active-encounter flag
      D:M6 produces primary_preferred_direction and numeric_value constraints to inform M5 MPC heading bounds
      I:mid_mpc_node.cpp:173-183 — M5 only checks active_rules.empty() as a flag, then reduces primary target CPA/TCPA by 80% as cost multiplier; pr
      ev:mid_mpc_node.cpp:173-184 — 3-line M6 consumption block with no direction or alteration field read
  DELTA[LOW] M6 runs at 2 Hz, spec states 1 Hz
      D:M6-spec.md §接口契约: 时间尺度 = 中时 (1 Hz)
      I:declare_parameter('reasoning_period_ms', 500) yields 2 Hz timer; comment at colregs_reasoner_node.cpp:448 explicitly says '2 Hz'
      ev:colregs_reasoner_node.cpp:231 (default 500 ms), line 448 (comment '2 Hz')
  DELTA[LOW] SAT3 forecast always 'nominal' — not encounter-urgency-aware
      D:CMM ADR-3: forecast(Δt)+uncertainty() must represent actual predicted system state with uncertainty
      I:publish_sat_data() line 723: sat3.predicted_state='nominal' hardcoded regardless of active encounters, timing phase, or conflict_detected
      ev:colregs_reasoner_node.cpp:723: 'msg.sat3.predicted_state = "nominal";'
  DELTA[LOW] Rule13 overtaking heuristic uses relative_speed_kn sign — misclassifies near-equal-speed encounters
      D:COLREGs Rule 13: overtaking determined by sector geometry and whether ownship is decisively faster than target
      I:rule13_overtaking.cpp:52 — 'if (kTargetHeadingSameDir && geo.relative_speed_kn > 0.0)' — any positive closing speed triggers GIVE_WAY; near-
      ev:rule13_overtaking.cpp:52-74 — binary sign check with no hysteresis threshold
  MOCK[MASQ] SAT3 forecast — always returns 'nominal' / 0.5 uncertainty @ colregs_reasoner_node.cpp:723-724
  MOCK[MASQ] SAT2 reasoning_chain — empty string published as if reasoning chain is serialized @ colregs_reasoner_node.cpp:718
  MOCK[stub] ASDR signature field intentionally left empty @ colregs_reasoner_node.cpp:743
  MOCK[MASQ] Rule16_GiveWay.evaluate() as standalone CPA gate dressed as COLREGs Rule16 obligation check @ src/l3_tdl_kernel/m6_colregs_reasoner/src/rules/colregs/rule16_give_way.cpp:26-43
  OVERCLAIM: M6-progress.md line 12: 'IDL + Arrow评分管线✅' for D2.4 status || reality:COLREGsConstraint IDL is defined and the 5-layer chain is built and emitted on /l3/m6/colregs_constr
  OVERCLAIM: M6-spec.md line 43: 'M6 推理已真实' (M6 reasoning is real) || reality:Mostly accurate for Rules 13-15, 17, 19. But Rule16 is a CPA-threshold proxy not a true Rule16 oblig
  FLOWGAP? M6 SAT2 → M8 ColregsRationaleTree M8 DEMO-2 ColregsRationaleTree frontend depends on SAT2Data.sat2.colregs_chain which M6 never populates. Chain
  FLOWGAP? M6 /l3/m6/rule_assessment → M4 (Rule13/15/17 coverage gap) rule_assessment is published only when Rule14 latch is active for the primary target. For Rule13 (overtaking) 
  FLOWGAP? M6 COLREGsConstraint heading direction → M5 MPC cost_colreg M6 emits primary_preferred_direction and per-rule numeric_value (minimum alteration degrees) on COLREGsConstra

### M7 — Doer-Checker (Checker role): IEC 61508 + SOTIF dual-track safety watchdog over M1-M6. Publishes /l3/m7/heartbeat, /l3/m7/safety_alert. Per spec also s
  PUB /l3/m7/safety_alert [PARTIAL_FIELDS] unpop:schema_version (always zero — no assignment in alert_generator.cpp or safety_arbitrator.cpp) :: safety_supervisor_node.cpp:209-211 pub_alert_ created; published in run_monitor_evaluation (lines 461-468) and publish_hard_constraint_alert (554-563). Fields stamp, severity, conf
  PUB /l3/checker/veto [MISSING] unpop: :: M7 node header (safety_supervisor_node.hpp:68-71) lists only pub_alert_, pub_asdr_, pub_sat_, pub_heartbeat_. No CheckerVetoNotification publisher exists anywhere in production C++
  PUB /l3/sat/data [PARTIAL_FIELDS] unpop:sat3 forecast fields (hardcoded empty),sat2.reasoning_chain (empty for all periodic ticks) :: safety_supervisor_node.cpp:217-219; on_sat_tick() at 10Hz publishes SAT data. sat3 forecast fields left at default per comment in alert_generator.cpp:75
  PUB /sil/sotif_metrics [STUB] unpop:violation_score (zeroed in stub mode),window_count (zeroed in stub mode),raw_value (zeroed in stub mode) :: sotif_metrics_publisher.cpp:29-37 — stub_mode_ defaults to true (sotif_metrics_publisher.hpp:21). When stub_mode_ is true, violation_score, window_count, raw_value are all hard-zer
  SUB /l3/m4/behavior_plan [DECLARED_UNUSED] :: safety_supervisor_node.cpp:285-290 — msg parameter is /*msg*/ (discarded); only watchdog kM4 heartbeat tick is recorded; plan content never inspected
  RESP[STUB] HC-1: CPA minimum distance hard constraint (<10ms end-to-end) :: core/hard_constraint_cpa.cpp implements compute_cpa_m7() and check_cpa_consistency() with real geometry. mrm_chain_executor.cpp:18 defines b
  RESP[MISSING] HC-2: UKC (under-keel clearance) hard constraint :: No UKC check function exists anywhere in m7_safety_supervisor/src. The spec lists it as HC-2 but there is no hard_constraint_ukc.cpp/hpp in 
  RESP[STUB] HC-3: ROT upper limit hard constraint :: core/hard_constraint_rot.cpp implements check_rot_limit() with real arithmetic and 5% tolerance. mrm_chain_executor references it in build_s
  RESP[STUB] HC-4: Speed upper limit hard constraint :: core/hard_constraint_speed.cpp implements check_speed_limit(). Same as HC-3 — function exists, never invoked from production path.
  RESP[MISSING] HC-5: ODD boundary check :: No ODD-boundary hard constraint function in m7_safety_supervisor (M1 does ODD scoring, but M7 spec requires an independent check). mrm_chain
  RESP[PARTIAL] HC-6: MRM trigger condition :: MrmSelector.select() + MrmChainExecutor.build_safety_alert_from_hard_constraints() define MRM logic. MrmSelector is called in run_monitor_ev
  RESP[PARTIAL] SOTIF assumption violation detection (5 classes per spec) :: sotif/assumption_monitor.cpp implements AIS/radar consistency, motion predictability, perception coverage, COLREGs solvability. CommLink ass
  RESP[PARTIAL] Doer-Checker triple quantification matrix (LOC ≥50:1, CC ≥30:1, SBOM ∩ = ∅) :: SBOM independence enforced by lint check in checker_verification.py:66-86 (no OR-Tools in M7). LOC/CC ratios not verified by any CI tool fou
  RESP[MISSING] CheckerVetoNotification publisher → M4/M5 as hard gate (spec:31) :: M7 has zero CheckerVetoNotification publishers. M7 only SUBSCRIBES to /l3/checker/veto. M4 and M5 source code contain no subscription to /l3
  RESP[MISSING] FMEDA M7 table ≥20 failure modes :: M7-spec.md:67 marks FMEDA as 🔴 未做. M7-progress.md:13 marks D3.3a as ✅ with 'FMEDA M7 v1.0' — contradiction. No FMEDA file found under docs/D
  RESP[MISSING] PATH-S CI 0-violation automated check :: M7-spec.md:68 marks as ⚫ 未验. Progress doc claims D3.3a ✅ (PATH-S CI通过) but spec still marks unverified. No CI config referencing m7 PATH-S f
  RESP[MISSING] CMM three-interface: current_state()/rationale()/forecast() :: M7 node exposes no current_state(), rationale(), or forecast(Δt)+uncertainty() ROS2 service or topic. SAT data (pub_sat_) provides a partial
  DELTA[CRITICAL] D3 (unverified): /l3/checker/veto is NEVER published — M7 has NO veto authority
      D:Spec (M7-spec.md:31): M7 publishes CheckerVetoNotification to M4/M5 as a synchronous hard gate — 'forced withdrawal to nominal'. ADR-2: Doer
      I:M7 node has exactly four publishers: pub_alert_, pub_asdr_, pub_sat_, pub_heartbeat_ (safety_supervisor_node.hpp:68-71). No CheckerVetoNotif
      ev:safety_supervisor_node.hpp:68-71 (publisher list); grep across all production .cpp/.py returns zero create_publisher calls for CheckerVetoNo
  DELTA[CRITICAL] All six HC functions are dead code — run_hard_constraint_checks is a no-op stub
      D:Spec: 6 hard constraints (CPA, UKC, ROT, speed, ODD boundary, MRM trigger) must be evaluated in <10ms on every avoidance plan arrival.
      I:run_hard_constraint_checks() at safety_supervisor_node.cpp:548-552 body is: `(void)now;` — exactly one statement that discards its argument.
      ev:safety_supervisor_node.cpp:548-552; grep for check_rot_limit/check_speed_limit/compute_cpa_m7/evaluate_dc_constraint in src/ (excluding test
  DELTA[HIGH] HC-2 UKC (under-keel clearance) constraint is completely absent from implementation
      D:Spec §HC list item 2: UKC (under-keel clearance) is a hard constraint.
      I:No hard_constraint_ukc.cpp/hpp exists. mrm_chain_executor.cpp handles HC-1 CPA, HC-2 COLREGs, HC-3 watchdog, HC-4 DC, HC-5 speed, HC-6 ROT —
      ev:File tree from codegraph_files shows no *ukc* file under m7_safety_supervisor; mrm_chain_executor.cpp function signature covers CPA/colregs/
  DELTA[HIGH] /sil/sotif_metrics publishes all-zeros in stub_mode (default, never disabled)
      D:M7-progress.md:21 claims 'M7 /sil/sotif_metrics @10Hz 已实装; 前端SotifMonitorStrip数据源就绪'. Spec:32: 6-metric aggregation for DEMO-2.
      I:sotif_metrics_publisher.hpp:21 `bool stub_mode_{true}` — default is stub. In stub mode (sotif_metrics_publisher.cpp:29-37) violation_score, 
      ev:sotif_metrics_publisher.hpp:21; sotif_metrics_publisher.cpp:29-37; grep for set_stub_mode returns only its own definition
  DELTA[HIGH] CommLink SOTIF assumption permanently zeroed — assumption #5 never evaluated
      D:Spec: SOTIF assumption class 5 = comms link RTT >2s / packet loss >20%.
      I:safety_supervisor_node.cpp:432: `sotif::CommLinkState const kCommLink{};` — zero-initialized struct (rtt_s=0, packet_loss_pct=0) passed to a
      ev:safety_supervisor_node.cpp:429-437
  DELTA[MEDIUM] on_behavior_plan discards message content — M4 behavior not inspected
      D:Spec: M7 subscribes to M4 BehaviorPlanMsg to monitor Doer outputs for constraint violations.
      I:on_behavior_plan() parameter is `const& /*msg*/` — payload discarded. Only the watchdog heartbeat tick for kM4 is recorded. M7 never checks 
      ev:safety_supervisor_node.cpp:285-290
  DELTA[MEDIUM] SafetyAlert schema_version always zero — CMM field violation
      D:Architecture §3 (CLAUDE.md): all messages must carry `schema_version`. M4 behavior_plan publishes schema_version=113 (observed live). M7 saf
      I:alert_generator.cpp builds SafetyAlert with stamp/severity/confidence/rationale/description/recommended_mrm but never sets schema_version. S
      ev:alert_generator.cpp:17-30 (no schema_version line); safety_arbitrator.cpp (no schema_version line); grep for schema_version in m7 src/ retur
  DELTA[MEDIUM] Gate-6 veto latency test hardcoded PASS stub
      D:Gate-6 Doer-Checker Independence must verify veto round-trip latency <50ms.
      I:checker_verification.py:123-125 run_veto_latency_test() returns `True, 'VETO latency test: Phase 2 (real M5/M7 ROS2 nodes not yet deployed)'
      ev:checker_verification.py:123-125
  DELTA[LOW] on_avoidance_plan zeroes extracted scalars immediately after storing plan
      D:M7 should extract speed/ROT/heading-change/DCPA from avoidance plan to feed HC checks.
      I:safety_supervisor_node.cpp:298-301: `last_avoidance_speed_ = 0.0F; last_avoidance_rot_ = 0.0F; last_avoidance_heading_change_ = 0.0F; last_a
      ev:safety_supervisor_node.cpp:297-301
  MOCK[MASQ] run_hard_constraint_checks() — complete no-op stub @ src/l3_tdl_kernel/m7_safety_supervisor/src/safety_supervisor_node.cpp:548-552
  MOCK[MASQ] SotifMetricsPublisher stub_mode=true (default, permanent) @ src/l3_tdl_kernel/m7_safety_supervisor/include/m7_safety_supervisor/sotif/sotif_metrics_publisher.hpp:21
  MOCK[stub] CommLinkState{} zero-init in run_monitor_evaluation @ src/l3_tdl_kernel/m7_safety_supervisor/src/safety_supervisor_node.cpp:432-437
  MOCK[MASQ] run_veto_latency_test() hardcoded pass @ src/sil_orchestrator/checker_verification.py:123-125
  MOCK[stub] publish_hard_constraint_alert() — unreachable dead code @ src/l3_tdl_kernel/m7_safety_supervisor/src/safety_supervisor_node.cpp:554-563
  MOCK[stub] build_safety_alert_from_hard_constraints() — defined, never called in production @ src/l3_tdl_kernel/m7_safety_supervisor/src/core/mrm_chain_executor.cpp:18
  OVERCLAIM: D3.3a Closed in ✅ 2026-05-25: M7-core: 6硬约束 + FMEDA M7 v1.0 + MRM chain + ResumeHandler + PATH-S CI通 || reality:6 hard constraint FUNCTIONS exist in source files but are completely disconnected from the runtime e
  OVERCLAIM: D3.3b Closed in ✅ 2026-05-25: SotifMetricsPublisher 已发布 @10Hz (M7-progress.md:14 + M7-progress.md:21 || reality:SotifMetricsPublisher publishes at correct frequency but in permanent stub_mode (default true, never
  OVERCLAIM: DEMO-2阻塞贡献 ✅ 已解除: M7 /sil/sotif_metrics @10Hz 已实装 (M7-progress.md:21) || reality:Topic publishes at 10Hz with correct structure, but actual metric values are all zeros due to perman
  FLOWGAP? /l3/checker/veto: nobody publishes → M7 subscribes (and M4/M5 never gate) M7 subscribes to /l3/checker/veto at safety_supervisor_node.cpp:174-178 and counts veto rate via VetoHandler +
  FLOWGAP? /l3/m7/safety_alert → M5/bridge: M5 ignores it, bridge has no execution gate M1 and M8 consume /l3/m7/safety_alert. M5 tactical planner and the docker bridge have no subscription to /l3/m
  FLOWGAP? HC scalar extraction from AvoidancePlan → HC functions: zeroed before use on_avoidance_plan() stores last_avoidance_ (the full plan) but immediately zeroes last_avoidance_speed_, last_
  FLOWGAP? CommLink state monitor: no publisher, always zero input to AssumptionMonitor SOTIF assumption #5 (comm RTT >2s / packet loss >20%) receives CommLinkState{rtt_s=0, packet_loss_pct=0} every

### M8 — 唯一对 ROC/船长说话的实体。聚合 M1-M7 的 CMM triplet（current_state/rationale/forecast），生成 SAT-1/2/3 透明性数据，管理 ToR 协议，发布 UIState 至前端；同时运行 Python FastAPI 后端处理 REST 和 W
  PUB /l3/m8/ui_state [PARTIAL_FIELDS] unpop:schema_version (never set, stays 0),ship_position (0.0 — comment says 'populated externally from nav filter' but M8 has no nav-filter subscription),ship_heading (0.0),ship_sog (0.0) :: ui_state_builder.cpp:19-51; line 48 comment: 'ship position/heading/sog remain 0.0 — populated externally from nav filter'; stamp IS set at hmi_transparency_bridge_node.cpp:288; co
  PUB /sil/sat2_data [STUB] unpop:ivp_contributions (empty array),colregs_chain (empty array),active_behavior,colregs_chain_target_id :: hmi_transparency_bridge_node.cpp:406-417 on_sil_stub_tick: rationale hardcoded 'sil_stub', trigger_reason='sil_stub', ivp_contributions and colregs_chain not populated; only publis
  PUB /sil/sat3_data [STUB] unpop:trajectory_candidates (empty array),uncertainty_bands :: hmi_transparency_bridge_node.cpp:419-432 on_sil_stub_tick: rationale='sil_stub', trajectory_candidates not populated, tdl_s=0.0, tmr_s=0.0; only published when !has_real_sat3_. CON
  PUB /sil/sotif_metrics [STUB] unpop:metrics[] (empty),active_violation_count=0 hardcoded,degradation_alert=false hardcoded :: hmi_transparency_bridge_node.cpp:434-446: only fires when !m7_active (M7 heartbeat timeout). In normal SIL operation M7 is live so M8 stub NEVER fires. M7 directly publishes to /si
  PUB /l3/m8/operator_state [MISSING] unpop: :: spec M8-spec.md §接口契约 lists /l3/m8/operator_state as a published topic; no publisher, no member variable pub_operator_state_, no topic string appears anywhere in hmi_transparency_b
  SUB /l3/m3/mission_goal [DECLARED_UNUSED] :: hmi_transparency_bridge_node.cpp:68-70 subscribes; on_mission_goal stores to latest_mission_ (line 190) but latest_mission_ is never referenced in on_
  RESP[PARTIAL] SAT-1/2/3 aggregation from all modules via /l3/sat/data CMM triplet :: SatAggregator ingest() at sat_aggregator.cpp:11-25 correctly stores sat1/sat2/sat3 per source module. However only M1 actually populates the
  RESP[PARTIAL] CMM triplet current_state()/rationale()/forecast() per-module aggregation for SAT dis :: sat_aggregator.cpp provides latest_sat1/sat2/sat3 accessors. adaptive_sat_trigger.cpp decide() uses aggregator. But UIState build context do
  RESP[STUB] ToR adaptive matrix 4-scenario (MUST from v3.0 scope, spec §v3.0 工时) :: grep for BNWAS, tor_matrix, 4.*scenario, urgent_tor in m8_hmi_transparency_bridge/ returns 0 hits. TorProtocol only has kIdle/kRequested/kAc
  RESP[STUB] active_role dual-role symmetric implementation (PRIMARY_ON_BOARD / PRIMARY_ROC / DUAL :: active_role.py ActiveRoleStateMachine class is implemented (active_role.py:29-65) with correct dual-ack logic. BUT: it is never instantiated
  RESP[PARTIAL] SAT-2/3/SOTIF bridge topics for Engineer view frontend panels :: Publishers exist for /sil/sat2_data /sil/sat3_data /sil/sotif_metrics (hmi_transparency_bridge_node.cpp:112-117). M8 stub fires only before 
  RESP[STUB] C++ ROS2 LifecycleNode per spec (§colcon 包 description) :: hmi_transparency_bridge_node.hpp:39: 'class HmiTransparencyBridgeNode : public rclcpp::Node'. No LifecycleNode, no on_configure/on_activate 
  RESP[PARTIAL] Python FastAPI backend with operator ToR acknowledge endpoint :: tor_endpoint.py POST /api/tor/acknowledge calls bridge.send_operator_action(). send_operator_action() at ros_bridge.py:88-103 has TODO(Phase
  RESP[MISSING] BNWAS-equivalent stub (v3.0 scope item) :: grep for BNWAS/bnwas in entire m8_hmi_transparency_bridge/ returns 0 hits
  RESP[MISSING] Y-axis Reflex Arc notification channel (v3.0 scope item) :: grep for reflex/Y.axis/Reflex_Arc in m8_hmi_transparency_bridge/ returns 0 hits. No subscription to any Y-axis bypass topic.
  RESP[MISSING] ECDIS integration stub (v3.1 D3.4 scope item) :: grep for ECDIS/S-100/IHO/IEC 61174/S-57 in m8_hmi_transparency_bridge/ returns 0 hits
  RESP[STUB] UIState 3-tier publish split: data_stream_50hz / display_state_4hz / alert_burst_even :: Single on_ui_publish_tick() at 50Hz (20ms timer, hmi_transparency_bridge_node.cpp:127-132). No 4Hz display_state timer. No alert_burst_event
  DELTA[CRITICAL] M8 is a parallel-publisher on /sil/sat2_data, /sil/sat3_data — not the aggregator
      D:M8 aggregates SAT-2/3 content from M4/M5/M6 and bridges it to the SIL frontend as the single source of truth for Engineer-view panels
      I:M4 publishes real SAT2Data (with ivp_contributions[], colregs_chain[]) directly to /sil/sat2_data (behavior_arbiter_node.cpp:70). M5 publish
      ev:behavior_arbiter_node.cpp:70 'create_publisher<SAT2Data>("/sil/sat2_data")'; mid_mpc_node.cpp:82 'create_publisher<SAT3Data>("/sil/sat3_data
  DELTA[HIGH] /l3/m8/operator_state topic specified in spec but does not exist in code
      D:M8 publishes /l3/m8/operator_state as one of its 12 SIL topics (M8-spec.md §接口契约 §真实发布 topic)
      I:No publisher for /l3/m8/operator_state anywhere in hmi_transparency_bridge_node.hpp or .cpp. No member pub_operator_state_. Topic string abs
      ev:grep for 'operator_state' in src/l3_tdl_kernel/m8_hmi_transparency_bridge returns 0 code hits (only conftest.py FakeBridge stub)
  DELTA[HIGH] UIState ship position/heading/sog fields permanently zero — M8 has no nav-filter subscription
      D:UIState published at 50Hz contains own-ship navigation state (position, heading, SOG) for HMI map display
      I:UIState.ship_position, ship_heading, ship_sog are never written; ui_state_builder.cpp:48 comment 'populated externally from nav filter' but 
      ev:hmi_transparency_bridge_node.cpp init_subscriptions() lines 54-97: 10 subscriptions, none to nav-filter; ui_state_builder.cpp:48 comment con
  DELTA[MEDIUM] M8 spec claims LifecycleNode but implementation inherits plain rclcpp::Node
      D:M8-spec.md §colcon包: 'C++ ROS LifecycleNode'; M8-progress.md D3.4 row and M8-spec.md §当前实现状态 row: 'C++ ROS LifecycleNode: ✅'
      I:class HmiTransparencyBridgeNode : public rclcpp::Node — no on_configure/on_activate/on_deactivate callbacks. SIL integration cannot observe 
      ev:hmi_transparency_bridge_node.hpp:39 'public rclcpp::Node'
  DELTA[MEDIUM] active_role dual-role machine never instantiated; UIState role hardcoded to ROC
      D:M8 implements active_role symmetric dual-role (PRIMARY_ON_BOARD / PRIMARY_ROC / DUAL_OBSERVATION) with dual-ack transition (v3.0 must-7 scop
      I:ActiveRoleStateMachine class defined in active_role.py but never instantiated in app.py or any endpoint. Role in UIState build context hardc
      ev:active_role.py:29 class exists; grep ActiveRoleStateMachine in web_server/ returns 0 callers outside tests; hmi_transparency_bridge_node.cpp
  DELTA[MEDIUM] ToR acknowledge HTTP endpoint returns True without triggering C++ TorProtocol state machine
      D:POST /api/tor/acknowledge forwards operator click to C++ TorProtocol via rclpy, which validates button-enabled state (SAT-1 displayed ≥5s) b
      I:send_operator_action() at ros_bridge.py:88-103 has TODO(Phase-E2) comment and always returns True without publishing any ROS2 message. C++ T
      ev:ros_bridge.py:100-103: 'TODO(Phase-E2): publish OperatorAction ROS2 message... return True'
  DELTA[MEDIUM] UIState schema_version field never set (stays 0)
      D:Architecture ADR: every message must carry schema_version field; M8 UIState at 50Hz is a CMM-mandated output
      I:UiStateBuilder::build() constructs l3_msgs::msg::UIState msg{} (line 19) with zero-init, then sets stamp/confidence/rationale/alert_counts/v
      ev:ui_state_builder.cpp:19-51: no schema_version assignment; contrast with M4 BehaviorPlan (schema_version=113 confirmed in live sample) and SA
  DELTA[MEDIUM] M7 SotifMetrics publisher stuck in stub_mode=true: all violation_score fields 0.0
      D:M7 publishes real SOTIF violation metrics to /sil/sotif_metrics for Engineer-view SotifMonitorStrip
      I:SotifMetricsPublisher::stub_mode_ defaults to true (sotif_metrics_publisher.hpp:21). set_stub_mode(false) is never called in safety_supervis
      ev:sotif_metrics_publisher.hpp:21 'bool stub_mode_{true}'; sotif_metrics_publisher.cpp:29-31 stub path zeroes all fields
  DELTA[LOW] latest_mission_ (M3 MissionGoal) subscribed and stored but never used in any publish path
      D:M8 integrates M3 mission context for SAT display and scenario inference
      I:on_mission_goal() stores to latest_mission_ (hmi_transparency_bridge_node.cpp:189-191) but latest_mission_ is absent from UiStateBuilder::Bu
      ev:hmi_transparency_bridge_node.cpp:190 'latest_mission_ = *msg'; member declared at hpp:93; search for 'latest_mission_' in hmi_transparency_b
  MOCK[MASQ] on_sil_stub_tick() SAT2Data publisher with rationale='sil_stub', empty ivp_contributions[], colregs_chain[] @ src/l3_tdl_kernel/m8_hmi_transparency_bridge/src/hmi_transparency_bridge_node.cpp:406-417
  MOCK[MASQ] on_sil_stub_tick() SAT3Data publisher with rationale='sil_stub', empty trajectory_candidates[], tdl_s=0, tmr_s=0 @ src/l3_tdl_kernel/m8_hmi_transparency_bridge/src/hmi_transparency_bridge_node.cpp:419-432
  MOCK[stub] on_sil_stub_tick() SotifMetrics publisher — only fires on M7 heartbeat timeout, all fields 0 @ src/l3_tdl_kernel/m8_hmi_transparency_bridge/src/hmi_transparency_bridge_node.cpp:434-446
  MOCK[MASQ] M7 SotifMetricsPublisher in stub_mode=true forever: publishes all-zero violation metrics as if they are real SOTIF asses @ src/l3_tdl_kernel/m7_safety_supervisor/include/m7_safety_supervisor/sotif/sotif_metrics_publisher.hpp:21
  MOCK[MASQ] send_operator_action() in RosBridge — TODO stub, always returns True without publishing ROS2 message @ src/l3_tdl_kernel/m8_hmi_transparency_bridge/python/web_server/ros_bridge.py:100-103
  MOCK[stub] ActiveRoleStateMachine in active_role.py — fully implemented class but zero callers; never instantiated @ src/l3_tdl_kernel/m8_hmi_transparency_bridge/python/web_server/active_role.py:29-65
  MOCK[stub] SilMockNode (sim_workbench mock_publisher) — publishes to /l3/sat/data with source_module='sil_mock_publisher' which Sat @ src/sim_workbench/mock_publishers/sil_mock_publisher/sil_mock_publisher/sil_mock_node.py:44; sat_aggregator.cpp:100-108
  OVERCLAIM: M8-progress.md D3.4 row: '✅ 2026-05-25 — SAT-2/3/SOTIF 桥接已实装' and 'M8 SAT-2/3/SOTIF 三 topic 已发布；前端 E || reality:The /sil/sat2_data, /sil/sat3_data, /sil/sotif_metrics publishers exist in M8, but M8's own publicat
  OVERCLAIM: M8-spec.md §当前实现状态 (2026-05-20): 'C++ ROS LifecycleNode: ✅' || reality:M8 inherits rclcpp::Node not rclcpp_lifecycle::LifecycleNode. No lifecycle state callbacks.
  OVERCLAIM: M8-progress.md D0.1 row: '✅ MUST-7 active_role stub' — implies stub exists and is functional || reality:ActiveRoleStateMachine class exists in active_role.py but is never instantiated or connected to any 
  OVERCLAIM: D3.4 report §DoD: 'ToR 协议（自适应超时）: ✅' and §已知遗留 'ToR 自适应矩阵: 🟡 当前基于 YAML 静态配置' || reality:No ToR adaptive matrix from YAML is wired into TorProtocol. M1 loads tor_matrix[] entries from its Y
  FLOWGAP? M4/M5 → /sil/sat2_data, /sil/sat3_data (M8 expected to be the sole bridge) Two ROS2 publishers on the same topic (/sil/sat2_data: M4 at 20Hz real + M8 at 1Hz stub; /sil/sat3_data: M5 at
  FLOWGAP? M8 /l3/m8/ui_state → frontend via foxglove_bridge /l3/m8/ui_state is bridged to /sil/m8_ui_state by sil_topic_bridge.py (line 386-389), but the frontend (useFox
  FLOWGAP? Operator browser click → C++ TorProtocol state machine POST /api/tor/acknowledge → send_operator_action() returns True without publishing ROS2 message → C++ TorProto
  FLOWGAP? M5/M4/M6 SATData → /l3/sat/data (design intent) vs direct /sil/ topic publishing (actual) SatAggregator cache entries for M4/M5/M6 sources remain empty because those modules publish directly to /sil/ 
  FLOWGAP? M8 publishes /l3/m8/tor_request → nobody subscribed (besides frontend indirectly) /l3/m8/tor_request is published by M8 (pub_tor_, hmi_transparency_bridge_node.cpp:107-108). M1 also publishes 

## ===== CROSS-CUTTING =====

### contracts — Audit of launch wiring, topic registry, CMM contract compliance, and dead msg types. Key findings: (1) CRITICAL — M5 publishes avoidance_plan to /m5/avoidance_plan but bridge+M7 subscribe to /l3/m5/av
  [CRITICAL/FLOW_GAP] CRITICAL: M5 publisher topic name mismatch — remap only in entrypoint, not in launch file
      If the system is ever launched via ros2 launch src/l3_tdl_kernel/launch/l3_pipeline.launch.py (the canonical launch file), M5's avoidance plan is publ
      ev:mid_mpc_node.cpp:79 publishes to '/m5/avoidance_plan'. bridge sil_topic_bridge.py:372 subscribes to '/l3/m5/avoidance_plan'. M7 safety_supervisor_node.cpp:145 s
  [HIGH/DESIGN_IMPL_DESYNC] veto_enabled: false in l3_params.yaml is a dead YAML key — M7 node never reads it
      The DEMO-1 intent to disable the X-axis Checker via this parameter has no effect. M7 is fully active (heartbeat confirmed live at 10Hz). If the intent
      ev:l3_params.yaml:39 sets 'veto_enabled: false' under m7_safety_supervisor. Searching safety_supervisor_node.cpp for 'veto_enabled', 'declare_parameter', 'stub_mod
  [HIGH/FLOW_GAP] /l3/m2/threat_state is live on DDS bus but has no publisher in any source file
      The bridge's geometry-release condition 1 (on_threat_state) can never fire because the topic has no publisher. Avoidance latch release relies entirely
      ev:Live topic list includes /l3/m2/threat_state. M2 world_model_node.cpp:271-284 setup_publishers() creates only three publishers: /l3/m2/world_state, /l3/sat/data
  [HIGH/STUB] SotifMetricsPublisher always runs in stub_mode (all violation scores = 0.0) on A4000
      The SOTIF assumption monitor (kAisRadarConsistency, kMotionPredictability, kPerceptionCoverage, kColregsSolvability, kCommLink, kCheckerVetoRate) neve
      ev:sotif_metrics_publisher.hpp:21 initializes stub_mode_=true. sotif_metrics_publisher.cpp:29-32 branches: if stub_mode_, all violation_score/window_count/raw_valu
  [HIGH/LEAKED_LOGIC] Bridge carries authoritative DCPA/TCPA geometry decision logic — ADR-4 Backseat Driver violation
      ADR-4 (Backseat Driver pattern) forbids vessel-specific constants in the A-layer decision core. The bridge contains: CPA_SAFE_M=1000.0m, SHIP_LENGTH_M
      ev:docker/sil_topic_bridge.py:776-832 implements _compute_dcpa_tcpa() with its own flat-earth CPA solver. Lines 834-865 _check_geometry_release() makes the avoidan
  [MEDIUM/DESIGN_IMPL_DESYNC] RuleAssessment.msg missing schema_version and confidence CMM fields
      CMM contract requires stamp + schema_version + confidence + rationale on every message. RuleAssessment lacks schema_version and rationale. This is a g
      ev:l3_msgs/msg/RuleAssessment.msg (6 lines): fields are applicable_rule (string), expected_action (string), confidence (float32), trigger_conditions (string[]), st
  [MEDIUM/DESIGN_IMPL_DESYNC] SafetyConcernEvent.msg missing schema_version, confidence, and rationale CMM fields
      CMM requires all four fields on every key message. SafetyConcernEvent is published to /l3/safety/concern (live topic) and consumed by the HMI/M8. With
      ev:l3_msgs/msg/SafetyConcernEvent.msg (7 lines): fields are concern_type (uint8), anchor_hdg (float32), suggested_action (string), severity (float32), stamp (built
  [MEDIUM/UNPOPULATED_FIELD] AvoidanceWaypoint CMM fields exist in .msg but are zeroed at runtime (confirmed by A4000 ground truth)
      This is the known D1 open keystone: when NLP solver succeeds, wp_gen_.generate() populates waypoints but whether it sets the CMM fields in generated w
      ev:AvoidanceWaypoint.msg:1-4 defines schema_version, stamp, confidence, rationale. A4000 live sample confirms per-waypoint schema_version=0, confidence=0.0, ration
  [MEDIUM/OTHER] SilTopicBridge is an extra glue node not in l3_pipeline.launch.py — carries COLAV state
      The bridge is architecturally necessary for SIL integration but carries avoidance state that architecturally belongs in M5 or M4. Its presence as an e
      ev:sil_topic_bridge.py contains SilTopicBridge class (ROS2 Node) that runs as the 'sil_topic_bridge' node. l3_pipeline.launch.py launches only M1-M8 (8 nodes, M7 i
  [LOW/DESIGN_IMPL_DESYNC] M7 veto_enabled param silently ignored — l3_pipeline.launch.py enable_m7:=false is the actual gate
      Low severity because the launch-file mechanism (enable_m7) is the correct and functional gate. The YAML key is dead weight causing documentation confu
      ev:l3_pipeline.launch.py:35-39 declares 'enable_m7' LaunchArgument (default 'true') used in IfCondition at line 124. This is the real on/off switch for M7. l3_para
  [LOW/OTHER] D2 (fallback VALID-forever) confirmed FIXED in current code
      Also confirmed: M4 TRANSIT teardown in bridge _on_behavior_plan (lines 611-629) provides an additional defensive release after _AVOID_TRANSIT_RELEASE_
      ev:mid_mpc_node.cpp:243-254: when behavior_plan_->behavior == BEHAVIOR_TRANSIT, an empty AvoidancePlan (no waypoints, status='NORMAL', confidence=1.0) is published
  [LOW/OTHER] D5 (conflict_detected misjudge) and D6 (RuleLatch early release) confirmed FIXED
      No additional evidence of regression found in current code.
      ev:D5: COLREGsConstraint.msg:7 'uint8 primary_role' field and 'string phase' field present; commit d8b0c608 noted in MEMORY.md as RESOLVED. D6: commit 158bba9d (br

### bridge — The SIL bridge layer is a substantial decision-logic layer that belongs in L3 but was implemented in Python glue code. It contains a full two-controller autopilot (heading PD + speed PI), a DCPA/TCPA 
  [CRITICAL/LEAKED_LOGIC] Full autopilot (HeadingController + SpeedController) lives in bridge, not L3
      The HeadingController and SpeedController are real ship-control algorithms. HeadingController uses Kp=1.0, rate-limited to 5 deg/s (transit) or 10 deg
      ev:docker/sil_topic_bridge.py:145-182 (HeadingController class, Kp=1.0, max_rate=5 deg/s), :163-182 (SpeedController, PI with integral clamping), :1288-1339 (_comp
  [CRITICAL/LEAKED_LOGIC] Avoidance arm/latch/teardown state machine fully in bridge
      The bridge independently decides when to arm avoidance (M5 plan has non-zero turn_radius_m), when to sustain it (M4 holds COLREG_AVOID behavior), and 
      ev:docker/sil_topic_bridge.py: _avoidance_active flag (:326), _avoidance_armed_time (:431), _LATCH_MIN_HOLD_S=8.0s (:431), _AVOID_TRANSIT_RELEASE_S=3.0s (:437), _o
  [CRITICAL/FLOW_GAP] M7 SafetyAlert NOT a hard gate on bridge actuator output (D3 unresolved)
      ADR-1 requires M7 to be a hard gate (Doer-Checker pattern). In the current SIL stack, M7 can issue MRC_REQUIRED or CRITICAL alerts and the bridge will
      ev:docker/sil_topic_bridge.py: no subscription to /l3/m7/safety_alert; bridge subscribes to /l3/checker/veto (:477-479) only for debug trace (_on_checker_veto :565
  [HIGH/LEAKED_LOGIC] DCPA/TCPA geometry computation duplicated in bridge (M2 should own this)
      M2 world_model is the authoritative world-view module (CLAUDE.md §3). CPA/TCPA should be computed once in M2 and published in ThreatState/TrackedTarge
      ev:docker/sil_topic_bridge.py:776-832 (_compute_dcpa_tcpa static method) — flat-earth DCPA/TCPA kinematic engine using COG/SOG from raw SilOwnShipState + TargetVes
  [HIGH/LEAKED_LOGIC] 60-degree heading clamp in bridge, not in M5/M4
      The 60-degree cap on avoidance heading deviation is a safety-significant parameter. It belongs in M6 (COLREGs constraint generator — minimum alteratio
      ev:docker/sil_topic_bridge.py:653-660 (in _on_behavior_plan): MAX_AVOID_DEV_DEG=60.0, clamps avoidance_target_heading_deg to nominal±60°. Identical logic repeated 
  [HIGH/LEAKED_LOGIC] Dead-stick open-loop fallback in bridge: fixed rudder when avoidance_active + target=None
      SHIP_LENGTH_M=46.0 (line 100) is a hardcoded FCB constant violating ADR-4 (Backseat Driver — zero ship constants). The turn-radius-to-rudder conversio
      ev:docker/sil_topic_bridge.py:1235-1245 (_compute_avoidance_autopilot): when _avoidance_target_heading_deg is None AND _last_avoidance_waypoint is not None, comput
  [HIGH/LEAKED_LOGIC] Cross-track error route-return controller in bridge
      Route-return control (post-avoidance heading back to planned route) should be M5 BC-MPC or M3/M4 transit behavior. The bridge implements an independen
      ev:docker/sil_topic_bridge.py:1265-1286 (_signed_xte_m, 22-line XTE geometry), :1305-1327 (_compute_transit_autopilot): XTE correction of 0.10 deg/m clamped to ±30
  [HIGH/MOCK] mock_l2_publisher synthesizes full L2 voyage task, planned route, speed profile, and replan response
      The mock synthesizes the entire L1/L2 interface. It always returns SUCCESS on any RouteReplanRequest regardless of reason (MRC_REQUIRED, ODD_EXIT, etc
      ev:docker/mock_l2_publisher.py:186-197 (publishers for /l1/voyage_task, /l2/planned_route, /l2/speed_profile, /l2/replan_response). Route source: scenario YAML nom
  [HIGH/MOCK] diagnostic_mock_publisher permanently masks M1 sensor degradation detection
      This mock permanently short-circuits M1's ODD envelope degradation logic. Any scenario requiring degraded-sensor behavior (SOTIF testing, HAZID inject
      ev:docker/diagnostic_mock_publisher.py:89-110 (_on_timer): always emits DiagnosticStatus.OK for radar/comm/tmr to /l3/diagnostics at 2 Hz. Comment at :13-18 explai
  [MEDIUM/UNPOPULATED_FIELD] AvoidanceWaypoint CMM fields (schema_version, confidence, rationale, stamp) never populated by NLP path
      The CMM interface contract (CLAUDE.md §3, architecture §15) mandates schema_version + confidence + rationale on every message. The NLP path (generate(
      ev:mid_mpc_waypoint_generator.cpp:87-130 (build_waypoints_): constructs AvoidanceWaypoint with only {position, safety_corridor_m, turn_radius_m, target_speed_kn, w
  [MEDIUM/DESIGN_IMPL_DESYNC] M5 publishes to /m5/avoidance_plan, not /l3/m5/avoidance_plan — remapping dependency in entrypoint shell script
      The C++ node uses a namespace-less topic. Correct routing depends entirely on the --ros-args -r remapping in a shell script. If M5 is launched outside
      ev:mid_mpc_node.cpp:79: pub_avoidance_plan_ = create_publisher<...>("/m5/avoidance_plan", 10). M7 subscribes to /l3/m5/avoidance_plan (safety_supervisor_node.cpp:1
  [MEDIUM/FLOW_GAP] fsm_aggregator publishes /l3/fsm_state but bridge only records it to trace, not to actuator gate
      STATE_HANDBACK is determined when M5 avoidance_plan has zero waypoints while behavior is COLREG_AVOIDANCE. This is the intended signal for post-avoida
      ev:docker/fsm_aggregator_node.py:125-126: publishes FsmState to /l3/fsm_state. docker/sil_topic_bridge.py:469-471: bridge subscribes /l3/fsm_state via _on_fsm_stat

### orchestrator — The orchestrator is structurally sound for Phase 1 (lifecycle, scenario CRUD, debug trace, scoring primary path). Six concrete gaps found: (1) ASDR /events endpoint has an unwired MessageCache — all c
  [HIGH/MOCK] ASDR /events: MessageCache permanently empty — cache-dependent events never fire
      The HMI ASDR decision ledger panel will always show only the INIT event regardless of what the kernel actually does. The cache wiring is a named TODO 
      ev:asdr_routes.py:1-9 header TODO: 'wire ROS2 subscribers to populate _msg_cache at runtime. Until then the cache is empty.' _msg_cache = MessageCache() at line 58
  [HIGH/FLOW_GAP] Backup auto-stop timer mutates _state to INACTIVE without issuing ROS2 DEACTIVATE
      The primary _auto_stop_timer() correctly calls await self.deactivate() (line 453) which sends the full ROS2 transition. The backup timer at line 466 b
      ev:lifecycle_bridge.py:459-473: _auto_stop_backup_timer() fires 30s after the primary timer. It does: self._state = LifecycleState.INACTIVE — a Python-only mutatio
  [HIGH/LEAKED_LOGIC] sil_topic_bridge: cpa_m/tcpa_s hardcoded 0.0 on every TrackedTarget published to M2
      The bridge-local _compute_dcpa_tcpa result is used only for the bridge's own latch-release logic (line 855-865). The corrected values are never forwar
      ev:docker/sil_topic_bridge.py:739-740: tgt.cpa_m = 0.0; tgt.tcpa_s = 0.0. The bridge computes real DCPA/TCPA internally via _compute_dcpa_tcpa() (line 776) for the
  [MEDIUM/STUB] Gate 1 WebSocket liveness check: unconditional PASS, never probes anything
      The 6-Gate GO/NO-GO verdict can pass even if the HMI WebSocket server is down. The comment 'WS state reported by frontend' implies intent to receive a
      ev:gate_runner.py:264-266: async def _check_ws_connected() -> tuple[str, str]: return CHECK_OK, 'WS state reported by frontend'. This function performs no I/O. Gat
  [MEDIUM/STUB] Gate 5 rosbag2 check: silent bypass when ROS2 is installed but recorder absent
      The bypass was introduced for evaluation sandboxes but the condition (has_ros2) fires on the production A4000 host. Real absence of rosbag2 is indisti
      ev:gate_runner.py:719-723: if has_ros2: return CHECK_OK, 'rosbag2 not running (dev/evaluation sandbox bypass — recording simulated)'. On A4000 (a full ROS2 Humble 
  [MEDIUM/STUB] Gate 6 VETO latency test: permanent Phase 2 stub always returns PASS
      The prior audit noted this as D3 (M1/M7 not a hard gate). The PID and container checks (lines 744-763) do run and can produce real FAIL results. But t
      ev:checker_verification.py:123-125: async def run_veto_latency_test() -> tuple[bool, str]: return True, 'VETO latency test: Phase 2 (real M5/M7 ROS2 nodes not yet 
  [MEDIUM/FLOW_GAP] POST /api/v1/ops/restart_node: shell substitution in exec-list form — always a docker no-op
      Fix requires either asyncio.create_subprocess_shell() with shell=True and the full command string, or replacing the subshell with a two-step: first ca
      ev:ops_routes.py:41: ok, msg = await _run(['docker', 'restart', f'$(docker ps -q --filter name={name})'], timeout=15.0). _run() at line 27 uses asyncio.create_subp
  [LOW/FLOW_GAP] scoring_routes.py /api/v1/vv/kpi: reads relative paths from CWD, not project root
      The files do exist at /Users/marine/Code/MASS-L3-Tactical Layer/test-results/ which confirms the pattern. A safer implementation would use Path(__file
      ev:scoring_routes.py:148-151: paths are 'test-results/kpi_p95_p99.json', 'test-results/coverage_cube.json', etc. — plain relative Path() instances. These resolve a
  [LOW/DESIGN_IMPL_DESYNC] scoring_routes.py rule_chain: always [] — M6 rule chain never wired to scoring output
      The rule_chain construction logic already exists in marzip_builder.py:175-186 (build_verdict). It is not reused in the REST endpoint. This is a copy-p
      ev:scoring_routes.py:102: 'rule_chain': [],  # populated by M6 in Phase 2. The HMI scoring panel displays rule_chain as the COLREGs compliance trace. The comment c

### frontend — The HMI has two data sources: Foxglove WebSocket (rosbridge-style via @tier4/roslibjs-foxglove) consuming 16 topics, and orchestrator REST API (/api/v1/…) for lifecycle/scenario/scoring/fault ops. Fiv
  [CRITICAL/DESIGN_IMPL_DESYNC] SotifMetrics wire format is structured array; HMI type expects flat named fields — complete schema mismatch
      Additionally /sil/sotif_metrics does not appear in the live A4000 topic list at all — the topic is published by M8 bridge (hmi_transparency_bridge_nod
      ev:Backend SotifMetrics.msg (src/l3_tdl_kernel/l3_msgs/msg/SotifMetrics.msg) defines SotifMetricEntry[6] metrics with fields assumption_id/violation_score/window_c
  [HIGH/FLOW_GAP] Five HMI-subscribed Foxglove topics have no backend publisher
      SensorStatusRow (web/src/screens/shared/SensorStatusRow.tsx:20) reads useTelemetryStore sensors → always []. CommLinkStatusRow (CommLinkStatusRow.tsx:
      ev:web/src/hooks/useFoxgloveLive.ts lines 77-95 subscribe to /sil/sensor_status, /sil/commlink_status, /sil/fault_status, /sil/control_cmd (all as sil_msgs/ModuleP
  [HIGH/DESIGN_IMPL_DESYNC] SAT2 IvP contributions: 6-element scalar array on wire, HMI expects IvpContribution[] objects with direction_deg
      Furthermore the M8 stub (hmi_transparency_bridge_node.cpp line 407-416) emits SAT2 with zero ivp_contributions and empty colregs_chain when has_real_s
      ev:SAT2Data.msg (src/l3_tdl_kernel/l3_msgs/msg/SAT2Data.msg) defines float32[6] ivp_contributions and string[6] ivp_labels. M4 backend (behavior_arbiter_node.cpp l
  [HIGH/MOCK] M5 popover 'best cost' and M2/M3 popover route/waypoint details are hardcoded placeholder strings
      The M1 ODD and M2 XTE fields have no backend source — /l3/m1/odd_state and /l3/m2/world_state exist in the live topic list but are not subscribed by t
      ev:SimulationMonitor.tsx lines 1558-1671 show: M1 popover hardcodes 'OPEN_WATER (开阔)' and '92% (符合SIL标准)'; M2 hardcodes 'SEG_XIAMEN_SHANGHAI_A', '0.02 nm', 'WP04 (
  [HIGH/FLOW_GAP] /sil/target_vessel_state absent from live topic list — target vessel display may be dark
      
      ev:The HMI subscribes to /sil/target_vessel_state (useFoxgloveLive.ts line 36, TOPIC_MAP entry). docker/sil_topic_bridge.py line 356 subscribes to /sil/target_vess
  [HIGH/FLOW_GAP] FSM state driven from /l3/fsm_state but TOR/MRC transitions from backend are not wired
      The /l3/m8/ui_state topic (published at 50Hz by M8 bridge) is also not subscribed by the HMI — the operator_state, sat_decision, and scenario context 
      ev:useFoxgloveLive.ts lines 131-142 subscribe to /l3/fsm_state and call useFsmStore._updateState(). However, the backend also publishes TOR triggers on /l3/m8/tor_
  [HIGH/DESIGN_IMPL_DESYNC] SAT-1/2/3 transparency: SAT-1 not implemented; SAT-2/SAT-3 partially implemented with schema gaps
      TrajectoryCandidate.msg (src/l3_tdl_kernel/l3_msgs/msg/TrajectoryCandidate.msg): geometry_msgs/Point[] waypoints with fields x,y,z. HMI TrajectoryCand
      ev:M8-spec (ADR-3) requires SAT-1 (operator situation awareness), SAT-2 (decision rationale), SAT-3 (forecast). /l3/sat/data exists in live topic list and is the S
  [MEDIUM/STUB] ConningBar sparkline permanently empty (Phase 2 placeholder)
      
      ev:web/src/screens/shared/ConningBar.tsx line 79: <Sparkline data={[/* Phase 2: 60s ring buffer */]} color="var(--c-phos)" /> — the data array is always empty. The
  [MEDIUM/DESIGN_IMPL_DESYNC] TCPA displayed with unit 'm' (meters) but value is minutes
      
      ev:SimulationMonitor.tsx line 319 parses ASDR event payload: { cpa: p.cpa_nm, tcpa: p.tcpa_min ?? 0 } — field name tcpa_min. Line 356 renders: const tcpa = tcpaVal
  [MEDIUM/MOCK] TOR hotkey injects hardcoded scenario description, not live /l3/m3/tor_request payload
      
      ev:SimulationMonitor.tsx lines 520-527: the 'T' key hotkey calls fsm.setTorRequest({ reason: 'Manual Operator Intervention Requested (Collision Hazard)', currentSi
  [MEDIUM/STUB] SAT2/SAT3 M8 stubs sent with empty payloads until real M4/M5 data arrives — SAT transparency layers blank on cold start
      
      ev:hmi_transparency_bridge_node.cpp lines 394-432: on_sil_stub_tick() publishes stub SAT2 with confidence=1.0, rationale='sil_stub', and all zero ivp_contributions
  [MEDIUM/MOCK] Avoidance decision card 'STR' field shows hardcoded heading string, ignoring live M5 output
      
      ev:SimulationMonitor.tsx lines 1253-1261: 'avoid-right' tab card for STR (避碰转向指令) renders: {fsmState === 'COLREG_AVOIDANCE' ? '右舵转向 15°' : '常规保向'}. This is purely 
  [MEDIUM/FLOW_GAP] /l3/m1/odd_state, /l3/m2/world_state, /l3/m2/threat_state, /l3/m7/safety_alert not subscribed by HMI
      
      ev:useFoxgloveLive.ts TOPIC_MAP (lines 17-143) lists only 16 topics. The following live topics have no HMI subscription: /l3/m1/odd_state (ODD parameters, water de

### sim — The SIL stack has a clear real/mock boundary. Own-ship dynamics (MMGModel/RK4, ShipDynamicsNode) and environment disturbance (Gauss-Markov) are genuinely simulated. Target kinematics (TargetVesselNode
  [CRITICAL/STUB] M5 NLP solver always fails (Restoration_Failed / Solved_To_Acceptable_Level) — geometric fallback runs in production
      The geometric fallback is correctly gated (M4 TRANSIT → empty plan; else → fallback arc). But it is presented to the audit chain as a solved-MPC outpu
      ev:mid_mpc_node.cpp:237-261: const bool solver_failed = (sol.status != MidMpcSolution::Status::Converged) || sol.trajectory.empty(). In practice the MPC NLP does n
  [HIGH/MOCK] KF tracker update() bypasses Kalman gain — direct state overwrite
      When tracker_type='kf', TargetVessel velocities fed to M2/M6 are always the initial vx=0,vy=0 extrapolated forward via F-matrix predict, with lat/lon 
      ev:src/sim_workbench/sil_nodes/tracker_mock/tracker_mock/node.py:45-51 — update() sets self.x[0]=zx, self.x[1]=zy directly with comment 'Simplified direct measurem
  [HIGH/DEAD_CODE] FMI bridge (dds_fmu_node + LibcosimWrapper) is dead code — never launched
      The FMI 2.0 integration path (libcosim → FMU → DDS → ROS2) was scaffolded for Phase 2 certification evidence but is wholly inert in the live SIL. All 
      ev:docker/sil_entrypoint.sh launches only: ShipDynamicsNode, TargetVesselNode, EnvDisturbanceNode, SensorMockNode, TrackerMockNode, FaultInjectionNode, ScoringNode
  [HIGH/DEAD_CODE] FcbSimulatorNode (C++) dead in production — wrong topic names, empty target list
      FcbSimulatorNode uses pluginlib to load a 'FCBSimulator' plugin whose class_loader path is 'ship_sim_interfaces/ship_sim::ShipMotionSimulator'. This p
      ev:src/sim_workbench/fcb_simulator/src/fcb_simulator_node.cpp:43 subscribes to '/m5/avoidance_plan' (old namespace); live kernel publishes to '/l3/m5/avoidance_pla
  [HIGH/FLOW_GAP] SensorMockNode output (/sil/radar_meas, /sil/ais_msg) not consumed by any kernel path
      The radar measurement pipeline (sensor→tracker→world model) is architecturally correct on paper but sensor noise/clutter injected by SensorMockNode is
      ev:SensorMockNode (src/sim_workbench/sil_nodes/sensor_mock/sensor_mock/node.py) publishes to /sil/radar_meas (5 Hz) and /sil/ais_msg (0.1 Hz). sil_topic_bridge.py 
  [HIGH/LEAKED_LOGIC] sil_topic_bridge carries DCPA/TCPA computation and 60° heading clamp — decision logic leaked from M4/M5
      ADR-4 (Backseat Driver) requires decision core to be vessel-class agnostic. The bridge has Kp=1.0, max_rate_deg_s=5.0/10.0 which are implicit vessel p
      ev:docker/sil_topic_bridge.py:322-326 instantiates HeadingController(Kp=1.0, max_rate_deg_s=5.0) and AvoidanceHeadingController(Kp=1.0, max_rate_deg_s=10.0). Lines
  [HIGH/UNPOPULATED_FIELD] M5 avoidance_plan per-waypoint CMM fields (schema_version, stamp, confidence, rationale) unpopulated on MPC-solved path
      The design-intent CMM mandate (CLAUDE.md §3: every message must carry stamp + schema_version + confidence + rationale) is satisfied on the fallback pa
      ev:mid_mpc_waypoint_generator.cpp:89-130 — wp.position, wp.safety_corridor_m, wp.turn_radius_m, wp.target_speed_kn, wp.wp_distance_m are set; schema_version, stamp
  [HIGH/DESIGN_IMPL_DESYNC] M7 veto fires SafetyAlert → M1 soft scoring only; no hard gate to M4/M5
      ADR-1 states 'M1 ODD state is the sole source of behavior switching'. In the current implementation the M7→M1 path is real but the M1→M4/M5 forcing is
      ev:safety_supervisor_node.cpp:457-463: alert is published to /l3/m7/safety_alert only when severity > INFO. odd_envelope_manager_node.cpp:795-800: M1 reads last_sa
  [MEDIUM/DEAD_CODE] AIS bridge (ais_bridge package) not wired into live entrypoint
      build_tracked_target_array sets cpa_m=0.0, tcpa_s=0.0 unconditionally (src/sim_workbench/ais_bridge/ais_bridge/target_publisher.py:58-59), so even if 
      ev:src/sim_workbench/ais_bridge/ contains AisReplayNode, dataset_loader (load_dma_nmea, load_noaa_csv), and target_publisher (build_tracked_target_array). No refer
  [MEDIUM/MOCK] TargetVesselNode kinematic model is linear (no MMG/RK4) — OU noise only in NCDM mode; ROT always 0
      For COLREG encounter scenarios (head-on, crossing), target vessel motion must be realistic enough to generate credible CPA/TCPA for M6. The linear mod
      ev:target_vessel/node.py:76-105 — TargetVessel.step() uses linear dead-reckoning: lat += sog*cos(heading)*dt/111120.0. NCDM mode adds OU heading perturbation; REPL
  [MEDIUM/MOCK] M8 on_sil_stub_tick() publishes internal SAT2/SAT3/SOTIF stubs at 1 Hz — masquerades as real M7 SOTIF output
      When stub_mode_=true in SotifMetricsPublisher, all six assumption metrics publish 0.0 regardless of real sensor state. The HMI operator sees green SOT
      ev:hmi_transparency_bridge_node.hpp:63: timer_sil_stub_ (1 Hz) calls on_sil_stub_tick(). pub_sil_sat2_, pub_sil_sat3_, pub_sil_sotif_ are created alongside real pu
  [MEDIUM/DESIGN_IMPL_DESYNC] FCB simulator subscribes /m5/avoidance_plan (old namespace) — would never receive live kernel output
      The FCB simulator's target array also hardcodes zero targets (line 215: msg.targets.clear()) with comment 'populate via scenario file later'. No scena
      ev:fcb_simulator_node.cpp:43: create_subscription('/m5/avoidance_plan', ...). Live kernel publishes to /l3/m5/avoidance_plan (per sil_entrypoint.sh line 285 remap:
  [MEDIUM/DESIGN_IMPL_DESYNC] scenario_authoring AisReplayNode publishes to /world_model/tracks (old namespace) — not consumed by M2
      Also: AisReplayNode._load_scenario() generates a constant-position trajectory (lat=np.full, lon=np.full) — the scenario YAML positions are held fixed 
      ev:src/sim_workbench/scenario_authoring/scenario_authoring/replay/ais_replay_node.py:49: self._pub = self.create_publisher(TrackedTargetArray, '/world_model/tracks
  [LOW/STUB] EnvDisturbanceNode wind model is Gauss-Markov — current model is constant placeholder
      M5 VesselDynamicsModel also uses zero sea-state (mid_mpc_node.cpp:210: const double hs_m = 0.0; // [TBD-HAZID]). So both the simulation-side disturban
      ev:env_disturbance/node.py:1 docstring: 'Current is constant (placeholder — replaced by tidal model in D2.5)'. Line 115-116: self._current_speed and self._current_
  [LOW/MOCK] ExternalMockPublisher (l3_external_mock_publisher) publishes to /fusion/* — conflicts with sil_topic_bridge in live SIL if both active
      The mock publishes ownship at 22.5N, 114.0E with sog=18kn cog=45° always — completely wrong for the Trondheim Fjord IMAZU scenarios (63.4N, 10.4E). If
      ev:src/sim_workbench/mock_publishers/l3_external_mock_publisher/l3_external_mock_publisher/external_mock_publisher.py:55-57: publishes to /fusion/tracked_targets, 

## ===== FLOW (non-CONNECTED) =====

### Perception -> World Model
  [HIGH] M2 -> M5: /l3/m2/world_state (target trajectory for MPC) = PARTIAL
      Mid-MPC correctly consumes /l3/m2/world_state and uses targets for MPC formulation. BC-MPC is silently starved because the topic string '/m2/world_sta | live:/l3/m2/world_state present in live list. '/m2/worl
  [HIGH] M2 -> ?: /l3/m2/threat_state (consumer identification) = BROKEN_NO_PUBLISHER
      The topic appears in the live topic list, which is anomalous given no publisher in source code. This is likely a ghost entry from a prior session or a | live:/l3/m2/threat_state IS present in the live topic l
  [MEDIUM] Fusion -> M2: /fusion/own_ship_state = PARTIAL
      The subscription and core position/speed fields are wired correctly. The yaw-rate field (r_dot_deg_s) present in FilteredOwnShipState is silently zero | live:/fusion/own_ship_state is present in the live topi
  [MEDIUM] M2 -> M4: /l3/m2/world_state = PARTIAL
      Functional data flows correctly for transit and geometry. The PARTIAL is because schema_version (CMM contract) and per-target rationale fields are pop | live:/l3/m2/world_state is present in the live topic li
  [LOW] M2 -> M1: /l3/m2/world_state = PARTIAL
      M1 receives the topic but uses it only as a heartbeat indicator. Actual world-state content (targets, CPA, ODD zone inputs) is not consumed by M1 from | live:/l3/m2/world_state is present in the live topic li

### Context + Rules -> Behavior
  [HIGH] M1 -> M4: /l3/m1/mode_cmd (HARD gate or soft read?) = PARTIAL
      Soft read for one value (EMERGENCY→MRC). MODE_LIMITED / MODE_DEGRADED produce zero behavioral change in arbitration_timer_callback. | live:/l3/m1/mode_cmd is in live topic list — topic is l
  [HIGH] M1 -> M5: speed/trajectory constraints (how delivered?) = BROKEN_NO_SUBSCRIBER
      Design intent per ADR-1: M1 ODD state should gate M5 solve or supply envelope constraints. Actual code: M5 derives all constraints from M4 behavior_pl | live:/l3/m1/odd_state is live but M5 never subscribes t
  [HIGH] M6 -> M5: /l3/m6/rule_assessment = BROKEN_NO_SUBSCRIBER
      Edge documented in phase-1 maps as gap. Confirmed: M5 NLP cost tuning cannot be rule-aware because rule_assessment never arrives at M5. | live:/l3/m6/rule_assessment is in live topic list. M5 n
  [HIGH] M6 -> M7: /l3/m6/colregs_constraint = PARTIAL
      Subscriber receives and parses the message. All downstream HC logic (HC-2 COLREGs geometry check) is dead code — the function body does nothing. | live:/l3/m6/colregs_constraint is live.
  [HIGH] M3 -> M1/M5/bridge: /l3/m3/mission_state = BROKEN_NO_PUBLISHER
      M3's internal MissionStateMachine tracks state (MissionState enum) but never publishes it as a ROS2 topic. M1 subscribes to /l3/m3/mission_state (per  | live:/l3/m3/mission_state IS in live topic list, but no
  [HIGH] L1/L2 -> M3: /l2/planned_route = MOCK_INTERCEPTED
      The topic is consumed correctly by both M3 and M5. The gap is that the publisher is MockL2Publisher in the bridge, not a real L2 voyage planner. | live:/l2/planned_route is live.
  [HIGH] L1/L2 -> M3: /l2/speed_profile = MOCK_INTERCEPTED
      Both M3 and M5 correctly consume the message. Real L2 publisher absent. | live:/l2/speed_profile is live.
  [MEDIUM] M1 -> M4: /l3/m1/odd_state (HARD gate or soft read?) = PARTIAL
      Soft read. Presence of odd_state gates standby vs active; actual zone value is not exploited for ODD-aware behavior parametrisation in M4. | live:/l3/m1/odd_state is in live topic list. Both publi
  [MEDIUM] M6 -> M4: /l3/m6/colregs_constraint (consumed as HARD constraint?) = PARTIAL
      PARTIAL: constraint is read and actively used to shape IvP solution space. But IvP solver falls back to geometric heading on infeasibility (line 475-5 | live:/l3/m6/colregs_constraint is live. Both M6 publish
  [MEDIUM] M6 -> M5: /l3/m6/colregs_constraint = PARTIAL
      M6 heading direction reaches M5 only indirectly via M4 IvP → behavior_plan window. The M6→M5 direct path carries only binary encounter flag and target | live:/l3/m6/colregs_constraint is live.
  [MEDIUM] L1/L2 -> M3: /l2/replan_response = MOCK_INTERCEPTED
      Subscriber logic is complete. Source is mock — real L2 replan loop is absent. Replan response will always be bridge-synthesised. | live:/l2/replan_response is live.

### Planning -> Actuation + Safety
  [CRITICAL] M5 -> M7: /l3/m5/avoidance_plan (for checking) = PARTIAL
      All six HC functions are dead code stubs per consolidated findings. The plan content arrives at M7 but the scalars extracted from it are zeroed before | live:/l3/m5/avoidance_plan is in live topic list.
  [CRITICAL] M7 -> M1: /l3/checker/veto, /l3/m7/safety_alert (synchronous gate?) = BROKEN_NO_PUBLISHER
      run_hard_constraint_checks at safety_supervisor_node.cpp:548-552 is literally: (void)now; return; — all six HC checks are dead. The veto publisher dec | live:/l3/checker/veto IS in live topic list (from mock,
  [HIGH] M5 -> bridge -> L4: /l3/m5/avoidance_plan -> /sil/actuator_cmd (bridge translates plan->actuator) = MOCK_INTERCEPTED
      The bridge is the authoritative actuator controller, not L4. M5's waypoints inform turn_radius_m as a fallback rudder angle only if no M4 avoidance ta | live:/sil/actuator_cmd is in live topic list. /l3/m5/av
  [HIGH] reflex/override: /l3/reflex/activation, /l3/override/active, /l3/m4/reactive_override_cmd = PARTIAL
      M7 on_override_cmd (line 351-357): calls watchdog_->on_message_received(MonitoredModule::kM3, kNow) — this is wrong, it feeds the M3 liveness counter  | live:/l3/reflex/activation IS in live topic list. /l3/o

### Kernel <-> Orchestrator <-> Web HMI
  [CRITICAL] M8 -> /sil/sat2_data -> HMI (via foxglove) = SCHEMA_MISMATCH
      The wire format mismatch means: HMI receives sat2.ivp_contributions as a 6-element number array but tries to render it as IvpContribution[] objects. A | live:/sil/sat2_data IS in the live topic list.
  [CRITICAL] M7/M8 -> /sil/sotif_metrics -> HMI (via foxglove) = SCHEMA_MISMATCH
      Double failure: schema mismatch makes the data unusable at the HMI even if it arrived, AND the topic is absent from the live list suggesting it is not | live:/sil/sotif_metrics is NOT in the live topic list (
  [HIGH] M8 -> /l3/m8/ui_state -> foxglove_bridge -> HMI = PARTIAL
      Three-level break: (1) HMI TOPIC_MAP has no subscription for /l3/m8/ui_state or /sil/m8_ui_state — the UIState feed is completely dark at the frontend | live:/l3/m8/ui_state IS in live topic list. /sil/m8_ui_
  [HIGH] M8 -> /l3/m8/operator_state -> M1 = BROKEN_NO_PUBLISHER
      M1's on_operator_state() callback (odd_envelope_manager_node.cpp:614-616) reads assumed_operator_state which feeds TMR computation. With no publisher, | live:/l3/m8/operator_state IS in the live topic list, w
  [HIGH] bridge -> sim: /sil/actuator_cmd = PARTIAL
      Functional in normal avoidance mode. Leaked control logic concern is architectural. Dead-stick open-loop: if avoidance_active+target=None, bridge issu | live:/sil/actuator_cmd IS in the live topic list.
  [HIGH] M8 -> /sil/sat3_data -> HMI (via foxglove) = SCHEMA_MISMATCH
      Even when M5 populates trajectory_candidates, the inner structure (points as lat/lon pairs vs a fixed-size C++ struct) likely mismatches the HMI's exp | live:/sil/sat3_data IS in the live topic list.
  [HIGH] Orchestrator REST /api/v1/asdr/events -> HMI = MOCK_INTERCEPTED
      The ASDR ledger displayed in the HMI is structurally a stub: 3 hardcoded time-gated events (INIT, SCENE_CHG, END) plus cache-dependent events that nev | live:REST endpoint exists at /api/v1/asdr/events. Topic
  [HIGH] HMI <- /sil/target_vessel_state (foxglove -> HMI) = BROKEN_NO_PUBLISHER
      If TargetVesselNode is not running or is publishing on a different topic, the HMI target vessel display is dark. The bridge correctly converts it to / | live:/sil/target_vessel_state is NOT in the live topic 
  [HIGH] Orchestrator backup-autostop -> Docker DEACTIVATE (lifecycle state machine) = BROKEN_NO_SUBSCRIBER
      Evidence from consolidated phase-1 map (orchestrator area, finding 5). Orchestrator _state mutation bypasses the actual docker/ROS2 deactivation comma | live:/api/v1/lifecycle endpoints are live.
  [MEDIUM] M8 -> /l3/m8/tor_request -> (HMI/M1 consumers) = BROKEN_NO_SUBSCRIBER
      The hotkey in SimulationMonitor.tsx:520-527 injects a hardcoded TorRequest object (reason='Manual Operator Intervention Requested...', recommendedMrm= | live:/l3/m8/tor_request IS in the live topic list. No k
  [MEDIUM] HMI -> /l3/fsm_state (foxglove -> fsmStore) = PARTIAL
      State updates from fsm_aggregator reach the HMI via foxglove. But the TOR branch is mock-injected client-side (hardcoded reason string) rather than re | live:/l3/fsm_state IS in the live topic list.
  [LOW] Orchestrator /api/v1/vv/kpi -> HMI (V&V KPI dashboard) = BROKEN_NO_PUBLISHER
      Data source files are test artifacts not generated at runtime. Relative-path bug means endpoint returns empty dict in production unless CWD is set cor | live:/api/v1/vv/kpi endpoint exists as a registered rou

## ===== VERIFIED (40 adversarial) =====
  [CRITICAL] M1: current_zone_ frozen at ODD_ZONE_A — zone dimension of ADR-001 is dead letter => CONFIRMED(HIGH)
  [CRITICAL] M5: BC-MPC node never launched — entire short-horizon layer is dead in production => CONFIRMED(HIGH)
  [CRITICAL] M7: D3 (unverified): /l3/checker/veto is NEVER published — M7 has NO veto authority => CONFIRMED(HIGH)
  [CRITICAL] M7: All six HC functions are dead code — run_hard_constraint_checks is a no-op stub => CONFIRMED(HIGH)
  [CRITICAL] M8: M8 is a parallel-publisher on /sil/sat2_data, /sil/sat3_data — not the aggregator => PARTIALLY_CONFIRMED(HIGH)
  [CRITICAL] contracts: CRITICAL: M5 publisher topic name mismatch — remap only in entrypoint, not in launch file => CONFIRMED(HIGH)
  [CRITICAL] bridge: Full autopilot (HeadingController + SpeedController) lives in bridge, not L3 => CONFIRMED(HIGH)
  [CRITICAL] bridge: Avoidance arm/latch/teardown state machine fully in bridge => CONFIRMED(HIGH)
  [CRITICAL] bridge: M7 SafetyAlert NOT a hard gate on bridge actuator output (D3 unresolved) => CONFIRMED(HIGH)
  [CRITICAL] frontend: SotifMetrics wire format is structured array; HMI type expects flat named fields — complete schema m => CONFIRMED(HIGH)
  [CRITICAL] sim: M5 NLP solver always fails (Restoration_Failed / Solved_To_Acceptable_Level) — geometric fallback ru => PARTIALLY_CONFIRMED(HIGH)
  [CRITICAL] FLOW:Planning -> Actuation + Safety: M5 -> M7: /l3/m5/avoidance_plan (for checking) = PARTIAL => CONFIRMED(HIGH)
  [CRITICAL] FLOW:Planning -> Actuation + Safety: M7 -> M1: /l3/checker/veto, /l3/m7/safety_alert (synchronous gate?) = BROKEN_NO_PUBLISHER => PARTIALLY_CONFIRMED(HIGH)
  [CRITICAL] FLOW:Kernel <-> Orchestrator <-> Web HMI: M8 -> /sil/sat2_data -> HMI (via foxglove) = SCHEMA_MISMATCH => PARTIALLY_CONFIRMED(HIGH)
  [CRITICAL] FLOW:Kernel <-> Orchestrator <-> Web HMI: M7/M8 -> /sil/sotif_metrics -> HMI (via foxglove) = SCHEMA_MISMATCH => CONFIRMED(HIGH)
  [HIGH] M1: ODDState and ModeCmd schema_version stays 0 — CMM contract broken => CONFIRMED(HIGH)
  [HIGH] M1: /l3/m1/tor_request has zero consumers — safety-critical ToR signal lost => CONFIRMED(HIGH)
  [HIGH] M1: MOCK masquerading: current_zone_ frozen at ODD_ZONE_A — zone dimension of M1 masquerades as real => CONFIRMED(HIGH)
  [HIGH] M1: MOCK masquerading: sat1.active_alerts hardcoded to empty list => CONFIRMED(HIGH)
  [HIGH] M2: schema_version never set on WorldState and TrackedTarget => CONFIRMED(HIGH)
  [HIGH] M2: /l3/m2/threat_state topic published nowhere — bridge subscriber starved => CONFIRMED(HIGH)
  [HIGH] M2: MUST-6 SOG validation is test-only dead code — production node skips it => CONFIRMED(HIGH)
  [HIGH] M2: MOCK masquerading: validate_sog() — MUST-6 SOG validation => CONFIRMED(HIGH)
  [HIGH] M2: MOCK masquerading: target classification by SOG threshold (fishing/passenger/cargo/tanker) => CONFIRMED(HIGH)
  [HIGH] M2: MOCK masquerading: os_msg.nav_mode hardcoded to 'OPTIMAL' => CONFIRMED(HIGH)
  [HIGH] M2: MOCK masquerading: EnvSanityChecker::validate() — only 3 of 7 checks implemented => CONFIRMED(HIGH)
  [HIGH] M3: Missing /l3/m3/mission_state publisher — M1 MRC context starved => CONFIRMED(HIGH)
  [HIGH] M3: speed_recommend_kn always 0 in MissionGoal — M4/M5 speed guidance missing => CONFIRMED(HIGH)
  [HIGH] M3: has_enc_check hardcoded true — 4-condition validity gate degraded to 3 conditions => CONFIRMED(HIGH)
  [HIGH] M3: MOCK masquerading: has_enc_check = true (ENC route validation stub) => CONFIRMED(HIGH)
  [HIGH] M3: MOCK masquerading: speed_recommend_kn = 0 (zero-default, never assigned) => CONFIRMED(HIGH)
  [HIGH] M3: MOCK masquerading: planned_eta_s = 0.0 passed to check_and_trigger_replan (no planned ETA tracking) => CONFIRMED(HIGH)
  [HIGH] M4: primary_preferred_direction from M6 ignored — starboard hardcoded for all encounters => CONFIRMED(HIGH)
  [HIGH] M4: /l3/m4/reactive_override_cmd publication missing — DEMO-2 P0 item absent => CONFIRMED(HIGH)
  [HIGH] M4: MOCK masquerading: Restricted_Visibility behavior — dead code, world_visibility_nm hardcoded 999.0 => CONFIRMED(HIGH)
  [HIGH] M4: MOCK masquerading: Channel_Follow / BERTH behavior — world_in_vts_zone never set, no IvP function bu => CONFIRMED(HIGH)
  [HIGH] M4: MOCK masquerading: dictionary_.set_priority_weight() called from on_rule_assessment() but weights ne => CONFIRMED(HIGH)
  [HIGH] M5: BC-MPC subscribes to wrong world_state namespace (/m2/ instead of /l3/m2/) => CONFIRMED(HIGH)
  [HIGH] M5: AvoidanceWaypoint CMM fields (schema_version, stamp, confidence, rationale) unpopulated in NLP succe => PARTIALLY_CONFIRMED(HIGH)
  [HIGH] M5: cost_colreg / cost_dist / cost_vel always zero — COLREGs cost term invisible in rationale => CONFIRMED(HIGH)

## ===== CRITIC =====
The consolidated M1-M8 + bridge + orchestrator + frontend audit was thorough on individual module topics, CMM field population, and the main ROS2 data flow edges. However, six distinct areas received zero or superficial coverage: (1) the fsm_aggregator_node glue layer and its /l3/fsm_state synthesis logic; (2) the shell_b_harness RL/evaluation simulator and its duplicate autopilot + hardcoded cpa_m=0 on every TrackedTarget; (3) the ScoringNode /l3/colregs_active subscription dead end; (4) the orchestrator lifecycle_bridge backup-autostop code path (partially noted but not code-verified); (5) fault_injection → kernel path (topic name mismatch between FaultInjectionNode and any kernel consumer
  [HIGH/FLOW_GAP] fsm_aggregator_node: /l3/m3/mission_state and /l3/m8/operator_state not subscribed
      ev:docker/fsm_aggregator_node.py:116-123 — FsmAggregatorNode subscribes only to /l3/m1/odd_state, /l3/m4/behavior_plan, /l3/m5/avoidance_plan, /l3/m7/safety_alert. It does NOT subscri
  [HIGH/MOCK] shell_b_harness: TrackedTarget.cpa_m and tcpa_s hardcoded 0.0 — M5/M6/M2 receive zero CPA
      ev:src/sim_workbench/shell_b_harness/shell_b_harness/simulator.py:472-473 — In ShellBSimulator.step() every published TrackedTarget has tgt.cpa_m = 0.0 and tgt.tcpa_s = 0.0. These are
  [HIGH/FLOW_GAP] ScoringNode: /l3/colregs_active subscription has no publisher anywhere in the codebase
      ev:src/sim_workbench/sil_nodes/scoring/scoring/node.py:72 — ScoringLifecycleNode.on_configure() subscribes to std_msgs/String on '/l3/colregs_active'. The _colregs_cb() at line 132 pa
  [HIGH/FLOW_GAP] orchestrator lifecycle_bridge: _auto_stop_backup_timer sets self._state=INACTIVE without ROS2 DEACTIVATE — previously noted but code confirms it
      ev:src/sil_orchestrator/lifecycle_bridge.py:459-473 — _auto_stop_backup_timer() at expiry sets self._state = LifecycleState.INACTIVE (line 466) and cancels self._timer_task but issues
  [HIGH/DESIGN_IMPL_DESYNC] shell_b_harness doer_composition: M3 and M7 not included — scoring and safety gate both absent from RL loop
      ev:src/sim_workbench/shell_b_harness/shell_b_harness/simulator.py:368-383 — doer_cmd includes m1_odd_manager, m2_world_model, m4_behavior_arbiter, m5_tactical_planner, m8_hmi_bridge. 
  [MEDIUM/MOCK] fsm_aggregator_node: active_rule hardcoded 'Rule 14 head-on' for ALL COLREG_AVOIDANCE encounters
      ev:docker/fsm_aggregator_node.py:184-188 — When M4 behavior_plan.behavior == BEHAVIOR_COLREG_AVOID the aggregator sets active_rule = 'Rule 14 head-on' unconditionally, regardless of w
  [MEDIUM/LEAKED_LOGIC] shell_b_harness: duplicate autopilot implementation diverges from bridge autopilot
      ev:src/sim_workbench/shell_b_harness/shell_b_harness/simulator.py:74-107, 539-593 — ShellBSimulator contains its own HeadingController and SpeedController identical in structure to th
  [MEDIUM/STUB] ScoringNode: t_action_s hardcoded 0.0 and behavior_phase hardcoded 'transit' — evasion timing score always zero
      ev:src/sim_workbench/sil_nodes/scoring/scoring/node.py:151-156 — _score_and_publish() calls HagenScorer.score_frame() with t_action_s=0.0 (time of avoidance action, should be derived 
  [MEDIUM/FLOW_GAP] ops_routes.py restart_node: shell substitution via exec-list form always a no-op
      ev:src/sil_orchestrator/ops_routes.py:41 — await _run(['docker', 'restart', f'$(docker ps -q --filter name={name})'], timeout=15.0). The string '$(docker ps -q --filter name=foo)' is 
  [MEDIUM/FLOW_GAP] fault_injection node: published fault topics (/sil/fault/ais_dropout etc.) consumed by no kernel node
      ev:src/sim_workbench/sil_nodes/fault_injection/fault_injection/node.py:17-21 — FaultInjectionNode publishes FaultEvent to /sil/fault/ais_dropout, /sil/fault/radar_spike, /sil/fault/di
  [LOW/STUB] useSchemaValidation: /api/v1/schema/fcb_traffic_situation returns 404 when scenarios dir absent
      ev:web/src/hooks/useSchemaValidation.ts:15 — fetch('/api/v1/schema/fcb_traffic_situation'). src/sil_orchestrator/schema_routes.py:8-9 — SCHEMA_FILE resolved as /var/sil/scenarios/fcb_
  [LOW/UNPOPULATED_FIELD] fsm_aggregator_node: /l3/fsm_state schema_version hardcoded 1 — does not match l3_msgs FsmState.msg versioning
      ev:docker/fsm_aggregator_node.py:202 — out.schema_version = 1. The l3_msgs convention observed elsewhere in the kernel uses schema_version values in the 111-121 range (e.g. 113 for So