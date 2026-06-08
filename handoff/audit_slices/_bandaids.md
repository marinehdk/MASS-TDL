# 创可贴 / 越界逻辑 / MOCK 目录（跨切层；spec 须剔除这些，progress 记为现状偏离）


## [bridge] The SIL bridge layer is a substantial decision-logic layer that belongs in L3 but was implemented in Python glue code. It contains a full two-controller autopilot (heading PD + speed PI), a DCPA/TCPA 
- [CRITICAL/LEAKED_LOGIC] Full autopilot (HeadingController + SpeedController) lives in bridge, not L3
    The HeadingController and SpeedController are real ship-control algorithms. HeadingController uses Kp=1.0, rate-limited to 5 deg/s (transit) or 10 deg/s (avoidance). SpeedController has integral anti-
    证据: docker/sil_topic_bridge.py:145-182 (HeadingController class, Kp=1.0, max_rate=5 deg/s), :163-182 (SpeedController, PI with integral clamping), :1288-1339 (_compute_transit_autopilot) and :1191-1253 (_
- [CRITICAL/LEAKED_LOGIC] Avoidance arm/latch/teardown state machine fully in bridge
    The bridge independently decides when to arm avoidance (M5 plan has non-zero turn_radius_m), when to sustain it (M4 holds COLREG_AVOID behavior), and when to release (geometry clear + TCPA<0 OR M4 ret
    证据: docker/sil_topic_bridge.py: _avoidance_active flag (:326), _avoidance_armed_time (:431), _LATCH_MIN_HOLD_S=8.0s (:431), _AVOID_TRANSIT_RELEASE_S=3.0s (:437), _on_avoidance_plan arming logic (:989-1101
- [CRITICAL/FLOW_GAP] M7 SafetyAlert NOT a hard gate on bridge actuator output (D3 unresolved)
    ADR-1 requires M7 to be a hard gate (Doer-Checker pattern). In the current SIL stack, M7 can issue MRC_REQUIRED or CRITICAL alerts and the bridge will continue publishing actuator commands unaffected.
    证据: docker/sil_topic_bridge.py: no subscription to /l3/m7/safety_alert; bridge subscribes to /l3/checker/veto (:477-479) only for debug trace (_on_checker_veto :565-573). M7 SafetySupervisorNode publishes
- [HIGH/LEAKED_LOGIC] DCPA/TCPA geometry computation duplicated in bridge (M2 should own this)
    M2 world_model is the authoritative world-view module (CLAUDE.md §3). CPA/TCPA should be computed once in M2 and published in ThreatState/TrackedTarget. The bridge computes its own CPA geometry indepe
    证据: docker/sil_topic_bridge.py:776-832 (_compute_dcpa_tcpa static method) — flat-earth DCPA/TCPA kinematic engine using COG/SOG from raw SilOwnShipState + TargetVesselState. Called from :834-865 (_check_g
- [HIGH/LEAKED_LOGIC] 60-degree heading clamp in bridge, not in M5/M4
    The 60-degree cap on avoidance heading deviation is a safety-significant parameter. It belongs in M6 (COLREGs constraint generator — minimum alteration magnitude) or M5 (NLP bounds). Having it in the 
    证据: docker/sil_topic_bridge.py:653-660 (in _on_behavior_plan): MAX_AVOID_DEV_DEG=60.0, clamps avoidance_target_heading_deg to nominal±60°. Identical logic repeated at :1072-1078 (in _on_avoidance_plan arm
- [HIGH/LEAKED_LOGIC] Dead-stick open-loop fallback in bridge: fixed rudder when avoidance_active + target=None
    SHIP_LENGTH_M=46.0 (line 100) is a hardcoded FCB constant violating ADR-4 (Backseat Driver — zero ship constants). The turn-radius-to-rudder conversion atan2(L, R) is a geometric approximation with no
    证据: docker/sil_topic_bridge.py:1235-1245 (_compute_avoidance_autopilot): when _avoidance_target_heading_deg is None AND _last_avoidance_waypoint is not None, computes rudder from wp.turn_radius_m via atan
- [HIGH/LEAKED_LOGIC] Cross-track error route-return controller in bridge
    Route-return control (post-avoidance heading back to planned route) should be M5 BC-MPC or M3/M4 transit behavior. The bridge implements an independent proportional XTE controller with hardcoded gain 
    证据: docker/sil_topic_bridge.py:1265-1286 (_signed_xte_m, 22-line XTE geometry), :1305-1327 (_compute_transit_autopilot): XTE correction of 0.10 deg/m clamped to ±30°, speed boost to 19.5 kn when |XTE|>150
- [HIGH/MOCK] mock_l2_publisher synthesizes full L2 voyage task, planned route, speed profile, and replan response
    The mock synthesizes the entire L1/L2 interface. It always returns SUCCESS on any RouteReplanRequest regardless of reason (MRC_REQUIRED, ODD_EXIT, etc.). The default route origin (63.44, 10.38 — Norwe
    证据: docker/mock_l2_publisher.py:186-197 (publishers for /l1/voyage_task, /l2/planned_route, /l2/speed_profile, /l2/replan_response). Route source: scenario YAML nominalRoute at :414-428, or straight-line 
- [HIGH/MOCK] diagnostic_mock_publisher permanently masks M1 sensor degradation detection
    This mock permanently short-circuits M1's ODD envelope degradation logic. Any scenario requiring degraded-sensor behavior (SOTIF testing, HAZID injection per D2.1) cannot be tested while this mock run
    证据: docker/diagnostic_mock_publisher.py:89-110 (_on_timer): always emits DiagnosticStatus.OK for radar/comm/tmr to /l3/diagnostics at 2 Hz. Comment at :13-18 explains the root cause: M1 defaults to {radar
- [MEDIUM/UNPOPULATED_FIELD] AvoidanceWaypoint CMM fields (schema_version, confidence, rationale, stamp) never populated by NLP path
    The CMM interface contract (CLAUDE.md §3, architecture §15) mandates schema_version + confidence + rationale on every message. The NLP path (generate() in waypoint generator) only populates top-level 
    证据: mid_mpc_waypoint_generator.cpp:87-130 (build_waypoints_): constructs AvoidanceWaypoint with only {position, safety_corridor_m, turn_radius_m, target_speed_kn, wp_distance_m}. Fields schema_version, co
- [MEDIUM/DESIGN_IMPL_DESYNC] M5 publishes to /m5/avoidance_plan, not /l3/m5/avoidance_plan — remapping dependency in entrypoint shell script
    The C++ node uses a namespace-less topic. Correct routing depends entirely on the --ros-args -r remapping in a shell script. If M5 is launched outside this entrypoint (e.g., in unit test or standalone
    证据: mid_mpc_node.cpp:79: pub_avoidance_plan_ = create_publisher<...>("/m5/avoidance_plan", 10). M7 subscribes to /l3/m5/avoidance_plan (safety_supervisor_node.cpp:145). M8 subscribes to /l3/m5/avoidance_p
- [MEDIUM/FLOW_GAP] fsm_aggregator publishes /l3/fsm_state but bridge only records it to trace, not to actuator gate
    STATE_HANDBACK is determined when M5 avoidance_plan has zero waypoints while behavior is COLREG_AVOIDANCE. This is the intended signal for post-avoidance handback. Bridge ignores it; instead it uses i
    证据: docker/fsm_aggregator_node.py:125-126: publishes FsmState to /l3/fsm_state. docker/sil_topic_bridge.py:469-471: bridge subscribes /l3/fsm_state via _on_fsm_state which calls only _trace_writer.record 

## [sim] The SIL stack has a clear real/mock boundary. Own-ship dynamics (MMGModel/RK4, ShipDynamicsNode) and environment disturbance (Gauss-Markov) are genuinely simulated. Target kinematics (TargetVesselNode
- [CRITICAL/STUB] M5 NLP solver always fails (Restoration_Failed / Solved_To_Acceptable_Level) — geometric fallback runs in production
    The geometric fallback is correctly gated (M4 TRANSIT → empty plan; else → fallback arc). But it is presented to the audit chain as a solved-MPC output (same publish_outputs_() call, same ASDR record)
    证据: mid_mpc_node.cpp:237-261: const bool solver_failed = (sol.status != MidMpcSolution::Status::Converged) || sol.trajectory.empty(). In practice the MPC NLP does not converge (per prior audit D1 and bran
- [HIGH/MOCK] KF tracker update() bypasses Kalman gain — direct state overwrite
    When tracker_type='kf', TargetVessel velocities fed to M2/M6 are always the initial vx=0,vy=0 extrapolated forward via F-matrix predict, with lat/lon snapped to measurement each cycle. Target SOG repo
    证据: src/sim_workbench/sil_nodes/tracker_mock/tracker_mock/node.py:45-51 — update() sets self.x[0]=zx, self.x[1]=zy directly with comment 'Simplified direct measurement update (full KF in Phase 2)'. KF cov
- [HIGH/DEAD_CODE] FMI bridge (dds_fmu_node + LibcosimWrapper) is dead code — never launched
    The FMI 2.0 integration path (libcosim → FMU → DDS → ROS2) was scaffolded for Phase 2 certification evidence but is wholly inert in the live SIL. All ship dynamics in production go through the Python 
    证据: docker/sil_entrypoint.sh launches only: ShipDynamicsNode, TargetVesselNode, EnvDisturbanceNode, SensorMockNode, TrackerMockNode, FaultInjectionNode, ScoringNode, ScenarioAuthoringNode, sil_topic_bridg
- [HIGH/DEAD_CODE] FcbSimulatorNode (C++) dead in production — wrong topic names, empty target list
    FcbSimulatorNode uses pluginlib to load a 'FCBSimulator' plugin whose class_loader path is 'ship_sim_interfaces/ship_sim::ShipMotionSimulator'. This plugin registry is not wired into the live sil_node
    证据: src/sim_workbench/fcb_simulator/src/fcb_simulator_node.cpp:43 subscribes to '/m5/avoidance_plan' (old namespace); live kernel publishes to '/l3/m5/avoidance_plan'. Line 211 publish_tracked_targets() c
- [HIGH/FLOW_GAP] SensorMockNode output (/sil/radar_meas, /sil/ais_msg) not consumed by any kernel path
    The radar measurement pipeline (sensor→tracker→world model) is architecturally correct on paper but sensor noise/clutter injected by SensorMockNode is silently discarded. M2 WorldModelNode receives ta
    证据: SensorMockNode (src/sim_workbench/sil_nodes/sensor_mock/sensor_mock/node.py) publishes to /sil/radar_meas (5 Hz) and /sil/ais_msg (0.1 Hz). sil_topic_bridge.py (docker/sil_topic_bridge.py:348-359) sub
- [HIGH/LEAKED_LOGIC] sil_topic_bridge carries DCPA/TCPA computation and 60° heading clamp — decision logic leaked from M4/M5
    ADR-4 (Backseat Driver) requires decision core to be vessel-class agnostic. The bridge has Kp=1.0, max_rate_deg_s=5.0/10.0 which are implicit vessel parameters. The 60° clamp was noted in prior audit 
    证据: docker/sil_topic_bridge.py:322-326 instantiates HeadingController(Kp=1.0, max_rate_deg_s=5.0) and AvoidanceHeadingController(Kp=1.0, max_rate_deg_s=10.0). Lines 431-437 implement _LATCH_MIN_HOLD_S=8.0
- [HIGH/UNPOPULATED_FIELD] M5 avoidance_plan per-waypoint CMM fields (schema_version, stamp, confidence, rationale) unpopulated on MPC-solved path
    The design-intent CMM mandate (CLAUDE.md §3: every message must carry stamp + schema_version + confidence + rationale) is satisfied on the fallback path but silently violated on the MPC-solved path (w
    证据: mid_mpc_waypoint_generator.cpp:89-130 — wp.position, wp.safety_corridor_m, wp.turn_radius_m, wp.target_speed_kn, wp.wp_distance_m are set; schema_version, stamp, confidence, rationale are never assign
- [HIGH/DESIGN_IMPL_DESYNC] M7 veto fires SafetyAlert → M1 soft scoring only; no hard gate to M4/M5
    ADR-1 states 'M1 ODD state is the sole source of behavior switching'. In the current implementation the M7→M1 path is real but the M1→M4/M5 forcing is indirect (ODD conformance degradation → M1 publis
    证据: safety_supervisor_node.cpp:457-463: alert is published to /l3/m7/safety_alert only when severity > INFO. odd_envelope_manager_node.cpp:795-800: M1 reads last_safety_alert_ and sets m7_critical flag. L
- [MEDIUM/DEAD_CODE] AIS bridge (ais_bridge package) not wired into live entrypoint
    build_tracked_target_array sets cpa_m=0.0, tcpa_s=0.0 unconditionally (src/sim_workbench/ais_bridge/ais_bridge/target_publisher.py:58-59), so even if wired it would feed zeroed CPA/TCPA fields to M2, 
    证据: src/sim_workbench/ais_bridge/ contains AisReplayNode, dataset_loader (load_dma_nmea, load_noaa_csv), and target_publisher (build_tracked_target_array). No reference to these modules appears in docker/
- [MEDIUM/MOCK] TargetVesselNode kinematic model is linear (no MMG/RK4) — OU noise only in NCDM mode; ROT always 0
    For COLREG encounter scenarios (head-on, crossing), target vessel motion must be realistic enough to generate credible CPA/TCPA for M6. The linear model is adequate for straight-line approach but will
    证据: target_vessel/node.py:76-105 — TargetVessel.step() uses linear dead-reckoning: lat += sog*cos(heading)*dt/111120.0. NCDM mode adds OU heading perturbation; REPLAY mode is constant heading. msg.rot = 0
- [MEDIUM/MOCK] M8 on_sil_stub_tick() publishes internal SAT2/SAT3/SOTIF stubs at 1 Hz — masquerades as real M7 SOTIF output
    When stub_mode_=true in SotifMetricsPublisher, all six assumption metrics publish 0.0 regardless of real sensor state. The HMI operator sees green SOTIF metrics even when assumption violations are occ
    证据: hmi_transparency_bridge_node.hpp:63: timer_sil_stub_ (1 Hz) calls on_sil_stub_tick(). pub_sil_sat2_, pub_sil_sat3_, pub_sil_sotif_ are created alongside real publishers. m7_safety_supervisor/src/sotif
- [MEDIUM/DESIGN_IMPL_DESYNC] FCB simulator subscribes /m5/avoidance_plan (old namespace) — would never receive live kernel output
    The FCB simulator's target array also hardcodes zero targets (line 215: msg.targets.clear()) with comment 'populate via scenario file later'. No scenario-file population mechanism is implemented.
    证据: fcb_simulator_node.cpp:43: create_subscription('/m5/avoidance_plan', ...). Live kernel publishes to /l3/m5/avoidance_plan (per sil_entrypoint.sh line 285 remap: '-r /m5/avoidance_plan:=/l3/m5/avoidanc
- [MEDIUM/DESIGN_IMPL_DESYNC] scenario_authoring AisReplayNode publishes to /world_model/tracks (old namespace) — not consumed by M2
    Also: AisReplayNode._load_scenario() generates a constant-position trajectory (lat=np.full, lon=np.full) — the scenario YAML positions are held fixed for 600s with linear interpolation, producing no m
    证据: src/sim_workbench/scenario_authoring/scenario_authoring/replay/ais_replay_node.py:49: self._pub = self.create_publisher(TrackedTargetArray, '/world_model/tracks', 10). M2 WorldModelNode (world_model_n
- [LOW/STUB] EnvDisturbanceNode wind model is Gauss-Markov — current model is constant placeholder
    M5 VesselDynamicsModel also uses zero sea-state (mid_mpc_node.cpp:210: const double hs_m = 0.0; // [TBD-HAZID]). So both the simulation-side disturbance and the planner's disturbance model are mismatc
    证据: env_disturbance/node.py:1 docstring: 'Current is constant (placeholder — replaced by tidal model in D2.5)'. Line 115-116: self._current_speed and self._current_dir are set once from parameters in on_a
- [LOW/MOCK] ExternalMockPublisher (l3_external_mock_publisher) publishes to /fusion/* — conflicts with sil_topic_bridge in live SIL if both active
    The mock publishes ownship at 22.5N, 114.0E with sog=18kn cog=45° always — completely wrong for the Trondheim Fjord IMAZU scenarios (63.4N, 10.4E). If accidentally co-launched with the real SIL it wou
    证据: src/sim_workbench/mock_publishers/l3_external_mock_publisher/l3_external_mock_publisher/external_mock_publisher.py:55-57: publishes to /fusion/tracked_targets, /fusion/own_ship_state, /fusion/environm

## [orchestrator] The orchestrator is structurally sound for Phase 1 (lifecycle, scenario CRUD, debug trace, scoring primary path). Six concrete gaps found: (1) ASDR /events endpoint has an unwired MessageCache — all c
- [HIGH/MOCK] ASDR /events: MessageCache permanently empty — cache-dependent events never fire
    The HMI ASDR decision ledger panel will always show only the INIT event regardless of what the kernel actually does. The cache wiring is a named TODO (10B-E) that was never completed. No ROS2 subscrib
    证据: asdr_routes.py:1-9 header TODO: 'wire ROS2 subscribers to populate _msg_cache at runtime. Until then the cache is empty.' _msg_cache = MessageCache() at line 58 is never populated by any subscriber. _
- [HIGH/FLOW_GAP] Backup auto-stop timer mutates _state to INACTIVE without issuing ROS2 DEACTIVATE
    The primary _auto_stop_timer() correctly calls await self.deactivate() (line 453) which sends the full ROS2 transition. The backup timer at line 466 bypasses deactivate() entirely and only sets the Py
    证据: lifecycle_bridge.py:459-473: _auto_stop_backup_timer() fires 30s after the primary timer. It does: self._state = LifecycleState.INACTIVE — a Python-only mutation. It does NOT call self._change_state(T
- [HIGH/LEAKED_LOGIC] sil_topic_bridge: cpa_m/tcpa_s hardcoded 0.0 on every TrackedTarget published to M2
    The bridge-local _compute_dcpa_tcpa result is used only for the bridge's own latch-release logic (line 855-865). The corrected values are never forwarded to M2/M6. This is a deliberate interim measure
    证据: docker/sil_topic_bridge.py:739-740: tgt.cpa_m = 0.0; tgt.tcpa_s = 0.0. The bridge computes real DCPA/TCPA internally via _compute_dcpa_tcpa() (line 776) for the geometry-release decision, but publishe
- [MEDIUM/STUB] Gate 1 WebSocket liveness check: unconditional PASS, never probes anything
    The 6-Gate GO/NO-GO verdict can pass even if the HMI WebSocket server is down. The comment 'WS state reported by frontend' implies intent to receive a frontend heartbeat, but no such mechanism exists 
    证据: gate_runner.py:264-266: async def _check_ws_connected() -> tuple[str, str]: return CHECK_OK, 'WS state reported by frontend'. This function performs no I/O. Gate 1 at line 146 calls it and appends '[o
- [MEDIUM/STUB] Gate 5 rosbag2 check: silent bypass when ROS2 is installed but recorder absent
    The bypass was introduced for evaluation sandboxes but the condition (has_ros2) fires on the production A4000 host. Real absence of rosbag2 is indistinguishable from a sandbox to this check.
    证据: gate_runner.py:719-723: if has_ros2: return CHECK_OK, 'rosbag2 not running (dev/evaluation sandbox bypass — recording simulated)'. On A4000 (a full ROS2 Humble install), pgrep finds no rosbag2 process
- [MEDIUM/STUB] Gate 6 VETO latency test: permanent Phase 2 stub always returns PASS
    The prior audit noted this as D3 (M1/M7 not a hard gate). The PID and container checks (lines 744-763) do run and can produce real FAIL results. But the latency test — the only quantitative timing pro
    证据: checker_verification.py:123-125: async def run_veto_latency_test() -> tuple[bool, str]: return True, 'VETO latency test: Phase 2 (real M5/M7 ROS2 nodes not yet deployed)'. Called by gate_6_doer_checke
- [MEDIUM/FLOW_GAP] POST /api/v1/ops/restart_node: shell substitution in exec-list form — always a docker no-op
    Fix requires either asyncio.create_subprocess_shell() with shell=True and the full command string, or replacing the subshell with a two-step: first call docker ps to get the ID, then call docker resta
    证据: ops_routes.py:41: ok, msg = await _run(['docker', 'restart', f'$(docker ps -q --filter name={name})'], timeout=15.0). _run() at line 27 uses asyncio.create_subprocess_exec(*cmd) — exec form, no shell.
- [LOW/FLOW_GAP] scoring_routes.py /api/v1/vv/kpi: reads relative paths from CWD, not project root
    The files do exist at /Users/marine/Code/MASS-L3-Tactical Layer/test-results/ which confirms the pattern. A safer implementation would use Path(__file__).resolve().parents[2] / 'test-results' as the b
    证据: scoring_routes.py:148-151: paths are 'test-results/kpi_p95_p99.json', 'test-results/coverage_cube.json', etc. — plain relative Path() instances. These resolve against the process CWD which is wherever
- [LOW/DESIGN_IMPL_DESYNC] scoring_routes.py rule_chain: always [] — M6 rule chain never wired to scoring output
    The rule_chain construction logic already exists in marzip_builder.py:175-186 (build_verdict). It is not reused in the REST endpoint. This is a copy-paste opportunity, not a fundamental missing implem
    证据: scoring_routes.py:102: 'rule_chain': [],  # populated by M6 in Phase 2. The HMI scoring panel displays rule_chain as the COLREGs compliance trace. The comment confirms this is a deliberate deferral, b

## [frontend] The HMI has two data sources: Foxglove WebSocket (rosbridge-style via @tier4/roslibjs-foxglove) consuming 16 topics, and orchestrator REST API (/api/v1/…) for lifecycle/scenario/scoring/fault ops. Fiv
- [CRITICAL/DESIGN_IMPL_DESYNC] SotifMetrics wire format is structured array; HMI type expects flat named fields — complete schema mismatch
    Additionally /sil/sotif_metrics does not appear in the live A4000 topic list at all — the topic is published by M8 bridge (hmi_transparency_bridge_node.cpp:116) but either not relayed by foxglove_brid
    证据: Backend SotifMetrics.msg (src/l3_tdl_kernel/l3_msgs/msg/SotifMetrics.msg) defines SotifMetricEntry[6] metrics with fields assumption_id/violation_score/window_count/is_violated/raw_value. HMI TypeScri
- [HIGH/FLOW_GAP] Five HMI-subscribed Foxglove topics have no backend publisher
    SensorStatusRow (web/src/screens/shared/SensorStatusRow.tsx:20) reads useTelemetryStore sensors → always []. CommLinkStatusRow (CommLinkStatusRow.tsx:13) reads commLinks → always []. ConningBar (Conni
    证据: web/src/hooks/useFoxgloveLive.ts lines 77-95 subscribe to /sil/sensor_status, /sil/commlink_status, /sil/fault_status, /sil/control_cmd (all as sil_msgs/ModulePulse or FaultEvent) and /sil/scoring (si
- [HIGH/DESIGN_IMPL_DESYNC] SAT2 IvP contributions: 6-element scalar array on wire, HMI expects IvpContribution[] objects with direction_deg
    Furthermore the M8 stub (hmi_transparency_bridge_node.cpp line 407-416) emits SAT2 with zero ivp_contributions and empty colregs_chain when has_real_sat2_ is false — so even when M4 is active and real
    证据: SAT2Data.msg (src/l3_tdl_kernel/l3_msgs/msg/SAT2Data.msg) defines float32[6] ivp_contributions and string[6] ivp_labels. M4 backend (behavior_arbiter_node.cpp lines 567-574) populates 6 slots from 8 d
- [HIGH/MOCK] M5 popover 'best cost' and M2/M3 popover route/waypoint details are hardcoded placeholder strings
    The M1 ODD and M2 XTE fields have no backend source — /l3/m1/odd_state and /l3/m2/world_state exist in the live topic list but are not subscribed by the HMI at all. The M8 RTT and alarm level fields h
    证据: SimulationMonitor.tsx lines 1558-1671 show: M1 popover hardcodes 'OPEN_WATER (开阔)' and '92% (符合SIL标准)'; M2 hardcodes 'SEG_XIAMEN_SHANGHAI_A', '0.02 nm', 'WP04 (24.460°N)'; M3 hardcodes 'WAYPOINT_TRACK
- [HIGH/FLOW_GAP] /sil/target_vessel_state absent from live topic list — target vessel display may be dark
    
    证据: The HMI subscribes to /sil/target_vessel_state (useFoxgloveLive.ts line 36, TOPIC_MAP entry). docker/sil_topic_bridge.py line 356 subscribes to /sil/target_vessel_state from DDS and relays it, but the
- [HIGH/FLOW_GAP] FSM state driven from /l3/fsm_state but TOR/MRC transitions from backend are not wired
    The /l3/m8/ui_state topic (published at 50Hz by M8 bridge) is also not subscribed by the HMI — the operator_state, sat_decision, and scenario context fields are unused.
    证据: useFoxgloveLive.ts lines 131-142 subscribe to /l3/fsm_state and call useFsmStore._updateState(). However, the backend also publishes TOR triggers on /l3/m8/tor_request and /l3/m3/tor_request (both in 
- [HIGH/DESIGN_IMPL_DESYNC] SAT-1/2/3 transparency: SAT-1 not implemented; SAT-2/SAT-3 partially implemented with schema gaps
    TrajectoryCandidate.msg (src/l3_tdl_kernel/l3_msgs/msg/TrajectoryCandidate.msg): geometry_msgs/Point[] waypoints with fields x,y,z. HMI TrajectoryCandidate type (web/src/types/sat.ts line 24): points:
    证据: M8-spec (ADR-3) requires SAT-1 (operator situation awareness), SAT-2 (decision rationale), SAT-3 (forecast). /l3/sat/data exists in live topic list and is the SATData aggregation — HMI does not subscr
- [MEDIUM/STUB] ConningBar sparkline permanently empty (Phase 2 placeholder)
    
    证据: web/src/screens/shared/ConningBar.tsx line 79: <Sparkline data={[/* Phase 2: 60s ring buffer */]} color="var(--c-phos)" /> — the data array is always empty. The Sparkline component (lines 18-30) retur
- [MEDIUM/DESIGN_IMPL_DESYNC] TCPA displayed with unit 'm' (meters) but value is minutes
    
    证据: SimulationMonitor.tsx line 319 parses ASDR event payload: { cpa: p.cpa_nm, tcpa: p.tcpa_min ?? 0 } — field name tcpa_min. Line 356 renders: const tcpa = tcpaVal != null ? `${tcpaVal.toFixed(1)} m` : '
- [MEDIUM/MOCK] TOR hotkey injects hardcoded scenario description, not live /l3/m3/tor_request payload
    
    证据: SimulationMonitor.tsx lines 520-527: the 'T' key hotkey calls fsm.setTorRequest({ reason: 'Manual Operator Intervention Requested (Collision Hazard)', currentSituation: 'Target EVT14A040 CPA < 0.15 NM
- [MEDIUM/STUB] SAT2/SAT3 M8 stubs sent with empty payloads until real M4/M5 data arrives — SAT transparency layers blank on cold start
    
    证据: hmi_transparency_bridge_node.cpp lines 394-432: on_sil_stub_tick() publishes stub SAT2 with confidence=1.0, rationale='sil_stub', and all zero ivp_contributions; stub SAT3 with empty trajectory_candid
- [MEDIUM/MOCK] Avoidance decision card 'STR' field shows hardcoded heading string, ignoring live M5 output
    
    证据: SimulationMonitor.tsx lines 1253-1261: 'avoid-right' tab card for STR (避碰转向指令) renders: {fsmState === 'COLREG_AVOIDANCE' ? '右舵转向 15°' : '常规保向'}. This is purely derived from FSM state — the hardcoded '
- [MEDIUM/FLOW_GAP] /l3/m1/odd_state, /l3/m2/world_state, /l3/m2/threat_state, /l3/m7/safety_alert not subscribed by HMI
    
    证据: useFoxgloveLive.ts TOPIC_MAP (lines 17-143) lists only 16 topics. The following live topics have no HMI subscription: /l3/m1/odd_state (ODD parameters, water depth, wind — displayed hardcoded in M1 po

## [contracts] Audit of launch wiring, topic registry, CMM contract compliance, and dead msg types. Key findings: (1) CRITICAL — M5 publishes avoidance_plan to /m5/avoidance_plan but bridge+M7 subscribe to /l3/m5/av
- [CRITICAL/FLOW_GAP] CRITICAL: M5 publisher topic name mismatch — remap only in entrypoint, not in launch file
    If the system is ever launched via ros2 launch src/l3_tdl_kernel/launch/l3_pipeline.launch.py (the canonical launch file), M5's avoidance plan is published to /m5/avoidance_plan while all consumers su
    证据: mid_mpc_node.cpp:79 publishes to '/m5/avoidance_plan'. bridge sil_topic_bridge.py:372 subscribes to '/l3/m5/avoidance_plan'. M7 safety_supervisor_node.cpp:145 subscribes to '/l3/m5/avoidance_plan'. Th
- [HIGH/DESIGN_IMPL_DESYNC] veto_enabled: false in l3_params.yaml is a dead YAML key — M7 node never reads it
    The DEMO-1 intent to disable the X-axis Checker via this parameter has no effect. M7 is fully active (heartbeat confirmed live at 10Hz). If the intent is to suppress veto-triggered MRC escalation for 
    证据: l3_params.yaml:39 sets 'veto_enabled: false' under m7_safety_supervisor. Searching safety_supervisor_node.cpp for 'veto_enabled', 'declare_parameter', 'stub_mode', and 'DEMO-1' returns zero matches. M
- [HIGH/FLOW_GAP] /l3/m2/threat_state is live on DDS bus but has no publisher in any source file
    The bridge's geometry-release condition 1 (on_threat_state) can never fire because the topic has no publisher. Avoidance latch release relies entirely on condition 2 (task_valid + TRANSIT via on_missi
    证据: Live topic list includes /l3/m2/threat_state. M2 world_model_node.cpp:271-284 setup_publishers() creates only three publishers: /l3/m2/world_state, /l3/sat/data, /l3/asdr/record. No other C++ or Pytho
- [HIGH/STUB] SotifMetricsPublisher always runs in stub_mode (all violation scores = 0.0) on A4000
    The SOTIF assumption monitor (kAisRadarConsistency, kMotionPredictability, kPerceptionCoverage, kColregsSolvability, kCommLink, kCheckerVetoRate) never surfaces real violations to the HMI/ASDR because
    证据: sotif_metrics_publisher.hpp:21 initializes stub_mode_=true. sotif_metrics_publisher.cpp:29-32 branches: if stub_mode_, all violation_score/window_count/raw_value = 0.0F. Searching safety_supervisor_no
- [HIGH/LEAKED_LOGIC] Bridge carries authoritative DCPA/TCPA geometry decision logic — ADR-4 Backseat Driver violation
    ADR-4 (Backseat Driver pattern) forbids vessel-specific constants in the A-layer decision core. The bridge contains: CPA_SAFE_M=1000.0m, SHIP_LENGTH_M=46.0m, MAX_RUDDER_DEG=35.0, CRUISE_SPEED_KN=10.0,
    证据: docker/sil_topic_bridge.py:776-832 implements _compute_dcpa_tcpa() with its own flat-earth CPA solver. Lines 834-865 _check_geometry_release() makes the avoidance release decision using bridge-local C
- [MEDIUM/DESIGN_IMPL_DESYNC] RuleAssessment.msg missing schema_version and confidence CMM fields
    CMM contract requires stamp + schema_version + confidence + rationale on every message. RuleAssessment lacks schema_version and rationale. This is a gap in the ASDR audit trail for M4's rule-consumpti
    证据: l3_msgs/msg/RuleAssessment.msg (6 lines): fields are applicable_rule (string), expected_action (string), confidence (float32), trigger_conditions (string[]), stamp (builtin_interfaces/Time), target_mm
- [MEDIUM/DESIGN_IMPL_DESYNC] SafetyConcernEvent.msg missing schema_version, confidence, and rationale CMM fields
    CMM requires all four fields on every key message. SafetyConcernEvent is published to /l3/safety/concern (live topic) and consumed by the HMI/M8. Without confidence and rationale, the SAT-1/2 transpar
    证据: l3_msgs/msg/SafetyConcernEvent.msg (7 lines): fields are concern_type (uint8), anchor_hdg (float32), suggested_action (string), severity (float32), stamp (builtin_interfaces/Time). Missing: schema_ver
- [MEDIUM/UNPOPULATED_FIELD] AvoidanceWaypoint CMM fields exist in .msg but are zeroed at runtime (confirmed by A4000 ground truth)
    This is the known D1 open keystone: when NLP solver succeeds, wp_gen_.generate() populates waypoints but whether it sets the CMM fields in generated waypoints needs verification. However the A4000 SUS
    证据: AvoidanceWaypoint.msg:1-4 defines schema_version, stamp, confidence, rationale. A4000 live sample confirms per-waypoint schema_version=0, confidence=0.0, rationale='', stamp.sec=0. Geometric fallback 
- [MEDIUM/OTHER] SilTopicBridge is an extra glue node not in l3_pipeline.launch.py — carries COLAV state
    The bridge is architecturally necessary for SIL integration but carries avoidance state that architecturally belongs in M5 or M4. Its presence as an extra non-M-numbered node is known but worth flaggi
    证据: sil_topic_bridge.py contains SilTopicBridge class (ROS2 Node) that runs as the 'sil_topic_bridge' node. l3_pipeline.launch.py launches only M1-M8 (8 nodes, M7 in GroupAction). SilTopicBridge is launch
- [LOW/DESIGN_IMPL_DESYNC] M7 veto_enabled param silently ignored — l3_pipeline.launch.py enable_m7:=false is the actual gate
    Low severity because the launch-file mechanism (enable_m7) is the correct and functional gate. The YAML key is dead weight causing documentation confusion.
    证据: l3_pipeline.launch.py:35-39 declares 'enable_m7' LaunchArgument (default 'true') used in IfCondition at line 124. This is the real on/off switch for M7. l3_params.yaml:39 'veto_enabled: false' is neve
- [LOW/OTHER] D2 (fallback VALID-forever) confirmed FIXED in current code
    Also confirmed: M4 TRANSIT teardown in bridge _on_behavior_plan (lines 611-629) provides an additional defensive release after _AVOID_TRANSIT_RELEASE_S=3.0s, independent of M5 plan content.
    证据: mid_mpc_node.cpp:243-254: when behavior_plan_->behavior == BEHAVIOR_TRANSIT, an empty AvoidancePlan (no waypoints, status='NORMAL', confidence=1.0) is published. Bridge sil_topic_bridge.py:1004-1007 g
- [LOW/OTHER] D5 (conflict_detected misjudge) and D6 (RuleLatch early release) confirmed FIXED
    No additional evidence of regression found in current code.
    证据: D5: COLREGsConstraint.msg:7 'uint8 primary_role' field and 'string phase' field present; commit d8b0c608 noted in MEMORY.md as RESOLVED. D6: commit 158bba9d (branch HEAD) is the fix for Rule-16 past-a