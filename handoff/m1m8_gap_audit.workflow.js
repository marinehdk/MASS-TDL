export const meta = {
  name: 'm1m8-gap-audit',
  description: 'Read-only M1-M8 + front/back-end gap audit: flow breaks (断流), mocks, design-vs-impl desync, with adversarial verification against stale findings',
  phases: [
    { title: 'Map', detail: '8 module + 5 cross-cutting mappers (sonnet) build pub/sub + design-vs-impl maps' },
    { title: 'Flow', detail: '4 flow-gap detectors adjudicate inter-module edges vs live topics' },
    { title: 'Verify', detail: 'adversarial verification of every high-value finding + completeness critic' },
  ],
}

const ROOT = '/Users/marine/Code/MASS-L3-Tactical Layer'

const BLACKLIST = `IGNORE these STALE backup dirs — they caused FALSE POSITIVES in a prior audit; do NOT read or cite them:
   - src/l3_tdl_kernel/m4_behavior_arbiter/.salvage-d3.1/
   - src/l3_tdl_kernel/m7_safety_supervisor/sotif/.salvage-d3.3b/
   - any path containing /archive/ or /Archive/`

const LIVE = `LIVE A4000 GROUND-TRUTH (branch fix/m5-nlp-convergence @158bba9d, SIL RUNNING now):
- All M1-M8 ROS2 topics ARE publishing. Measured rates: /l3/m2/world_state 20Hz, /l3/m4/behavior_plan 20Hz, /l3/m5/avoidance_plan 5Hz, /l3/m6/colregs_constraint 10Hz, /sil/actuator_cmd 5Hz.
- Sample /l3/m4/behavior_plan = HEALTHY (schema_version=113, behavior=1, heading[55,158], confidence=0.95, rationale='IvP: best_util=1.1 cells_feasible=7425').
- Sample /l3/m5/avoidance_plan = SUSPECT: per-waypoint schema_version=0, confidence=0.0, rationale='', stamp.sec=0 — CMM-mandated fields UNPOPULATED on waypoints.
- A4000 has uncommitted working-tree edits to mid_mpc_solver.cpp + colregs_reasoner_node.cpp.`

const PRIOR = `PRIOR AUDIT (docs/Doc From Claude/2026-06-08-avoidance-design-vs-implementation-gap.md) — already known; do NOT re-report verbatim. Instead VERIFY against current code and find what it MISSED:
- D1 (M5 NLP intermittent Restoration_Failed + cost_colreg≡0): OPEN keystone.
- D2 (fallback marked VALID-forever): claimed FIXED (mid_mpc_node.cpp:243-254 empty-plan gate + DEGRADED status).
- D3 (M1/M7 not a hard gate, ADR-1 weakened): UNVERIFIED — verify current M7 veto / M1 gating.
- D4 (bridge carries DCPA/TCPA + 60° clamp + latch): partially present — quantify what decision logic still lives in docker/sil_topic_bridge.py.
- D5 (conflict_detected misjudge): claimed FIXED (role-derived, commit d8b0c608).
- D6 (RuleLatch early release): claimed FIXED (commit 158bba9d).`

const LIVE_TOPICS = `/clock /cmd_tau /control/heading_setpoint /control/speed_setpoint /env/total_load /env/water_depth /fusion/environment_state /fusion/own_ship_state /fusion/tracked_targets /inject_fault /l1/voyage_task /l2/planned_route /l2/replan_response /l2/speed_profile /l3/asdr/record /l3/checker/veto /l3/colregs_active /l3/diagnostics /l3/fsm_state /l3/m1/mode_cmd /l3/m1/odd_state /l3/m1/tor_request /l3/m2/threat_state /l3/m2/world_state /l3/m3/mission_goal /l3/m3/mission_state /l3/m3/route_replan_request /l3/m3/tor_request /l3/m4/behavior_plan /l3/m4/reactive_override_cmd /l3/m5/avoidance_plan /l3/m6/colregs_constraint /l3/m6/rule_assessment /l3/m7/heartbeat /l3/m7/safety_alert /l3/m8/operator_state /l3/m8/tor_request /l3/m8/ui_state /l3/override/active /l3/reflex/activation /l3/safety/concern /l3/sat/data /l4/tracking_error /manual_actuator_cmd /override/active_signal /propulsion/constraints /reflex/activation_notification /route_planning/origin_latlon /route_planning/route_plan /ship/geo_position /ship/heading /ship/odometry /ship/waypoints /sil/actuator_cmd /sil/ais_msg /sil/asdr_event /sil/bridge_state /sil/environment /sil/fault/ais_dropout /sil/fault/dist_step /sil/fault/radar_spike /sil/lifecycle_status /sil/m8_ui_state /sil/module_pulse /sil/own_ship_state /sil/radar_meas /sil/sat2_data /sil/sat3_data`

function preamble() {
  return `READ-ONLY code auditor for the MASS-L3 Tactical Layer (ROS2 C++ kernel M1-M8 + Python orchestrator/bridge + Vite web HMI). Repo root: ${ROOT} (the path contains a SPACE).
RULES:
1. READ-ONLY — never edit/write/build/run/modify any file or run state-changing git. Pure reconnaissance.
2. Use codegraph for code (token-efficient): FIRST call ToolSearch with query "select:mcp__codegraph__codegraph_explore,mcp__codegraph__codegraph_search,mcp__codegraph__codegraph_files", then use codegraph_explore as your PRIMARY tool (pass it symbol/file names; it returns verbatim source). Use the Read tool for .md docs and .msg/.srv contracts (codegraph does NOT index those).
3. ${BLACKLIST}
4. EVERY finding MUST cite a CURRENT (non-salvage) file:line. No claims from memory. If you can't find current-code evidence, drop it.
5. "Gap" = (a) design INTENT (spec/architecture/progress docs) diverges from ACTUAL code, OR (b) a data-flow that breaks between modules (断流), OR (c) a MOCK/stub masquerading as real.`
}

const CTX = `\n\n${LIVE}\n\n${PRIOR}`

// ---------- schemas ----------
const MODULE_SCHEMA = {
  type: 'object',
  required: ['module', 'role', 'subscriptions', 'publications', 'responsibilities', 'design_impl_deltas', 'mocks_stubs', 'progress_doc_overclaims'],
  properties: {
    module: { type: 'string' },
    role: { type: 'string' },
    subscriptions: { type: 'array', items: { type: 'object', required: ['topic', 'status', 'evidence'], properties: {
      topic: { type: 'string' }, msg_type: { type: 'string' }, source: { type: 'string' },
      status: { type: 'string', enum: ['USED', 'DECLARED_UNUSED', 'EXPECTED_MISSING'] }, evidence: { type: 'string' } } } },
    publications: { type: 'array', items: { type: 'object', required: ['topic', 'status', 'evidence'], properties: {
      topic: { type: 'string' }, msg_type: { type: 'string' },
      status: { type: 'string', enum: ['REAL', 'PARTIAL_FIELDS', 'STUB', 'MISSING'] },
      unpopulated_fields: { type: 'array', items: { type: 'string' } }, evidence: { type: 'string' } } } },
    responsibilities: { type: 'array', items: { type: 'object', required: ['design_item', 'status', 'evidence'], properties: {
      design_item: { type: 'string' }, status: { type: 'string', enum: ['REAL', 'PARTIAL', 'STUB', 'MOCK', 'MISSING'] }, evidence: { type: 'string' }, note: { type: 'string' } } } },
    design_impl_deltas: { type: 'array', items: { type: 'object', required: ['title', 'severity', 'design_says', 'impl_does', 'evidence'], properties: {
      title: { type: 'string' }, severity: { type: 'string', enum: ['CRITICAL', 'HIGH', 'MEDIUM', 'LOW'] },
      design_says: { type: 'string' }, impl_does: { type: 'string' }, evidence: { type: 'string' } } } },
    mocks_stubs: { type: 'array', items: { type: 'object', required: ['what', 'file_line', 'masquerades_as_real'], properties: {
      what: { type: 'string' }, file_line: { type: 'string' }, masquerades_as_real: { type: 'boolean' }, note: { type: 'string' } } } },
    progress_doc_overclaims: { type: 'array', items: { type: 'object', required: ['claim', 'reality', 'evidence'], properties: {
      claim: { type: 'string' }, reality: { type: 'string' }, evidence: { type: 'string' } } } },
    flow_gaps_suspected: { type: 'array', items: { type: 'object', properties: { edge: { type: 'string' }, symptom: { type: 'string' }, evidence: { type: 'string' } } } },
  },
}

const GENERIC_SCHEMA = {
  type: 'object',
  required: ['area', 'summary', 'findings'],
  properties: {
    area: { type: 'string' },
    summary: { type: 'string' },
    findings: { type: 'array', items: { type: 'object', required: ['title', 'category', 'severity', 'evidence'], properties: {
      title: { type: 'string' },
      category: { type: 'string', enum: ['FLOW_GAP', 'MOCK', 'DESIGN_IMPL_DESYNC', 'LEAKED_LOGIC', 'UNPOPULATED_FIELD', 'DEAD_CODE', 'STUB', 'OTHER'] },
      severity: { type: 'string', enum: ['CRITICAL', 'HIGH', 'MEDIUM', 'LOW'] },
      detail: { type: 'string' }, evidence: { type: 'string' } } } },
    topic_io: { type: 'array', items: { type: 'object', properties: { topic: { type: 'string' }, direction: { type: 'string', enum: ['CONSUME', 'PRODUCE', 'REMAP', 'MOCK'] }, note: { type: 'string' } } } },
  },
}

const FLOW_SCHEMA = {
  type: 'object',
  required: ['segment', 'edges'],
  properties: {
    segment: { type: 'string' },
    edges: { type: 'array', items: { type: 'object', required: ['edge', 'verdict', 'severity', 'evidence'], properties: {
      edge: { type: 'string' },
      verdict: { type: 'string', enum: ['CONNECTED', 'BROKEN_NO_SUBSCRIBER', 'BROKEN_NO_PUBLISHER', 'MOCK_INTERCEPTED', 'SCHEMA_MISMATCH', 'FIELD_UNPOPULATED', 'PARTIAL'] },
      severity: { type: 'string', enum: ['CRITICAL', 'HIGH', 'MEDIUM', 'LOW', 'OK'] },
      detail: { type: 'string' }, evidence: { type: 'string' }, live_cross_check: { type: 'string' } } } },
  },
}

const VERDICT_SCHEMA = {
  type: 'object',
  required: ['verdict', 'confidence', 'reason'],
  properties: {
    verdict: { type: 'string', enum: ['CONFIRMED', 'REFUTED', 'PARTIALLY_CONFIRMED', 'STALE_ALREADY_FIXED'] },
    confidence: { type: 'string', enum: ['HIGH', 'MEDIUM', 'LOW'] },
    reason: { type: 'string' },
    current_code_quote: { type: 'string' },
  },
}

// ---------- module scopes ----------
const SPEC = (n, dir) => `docs/Design/TDL-Kernel/${dir}`
const MODULES = [
  { id: 'M1', name: 'ODD/Envelope Manager', sec: 5,
    spec: 'docs/Design/TDL-Kernel/M1-ODD-Envelope-Manager/M1-spec.md', progress: 'docs/Design/TDL-Kernel/M1-ODD-Envelope-Manager/M1-progress.md',
    node: 'src/l3_tdl_kernel/m1_odd_envelope_manager/src/odd_envelope_manager_node.cpp',
    pub: '/l3/m1/mode_cmd, /l3/m1/odd_state, /l3/m1/tor_request',
    extra: 'M1 = "唯一安全语境权威" (ADR-1: ODD is the single authority for behavior switching). KEY CHECK: is M1 a HARD gate (other modules MUST obey mode_cmd/odd_state) or only an advisory publisher others softly read? Trace who actually subscribes to AND gates on /l3/m1/mode_cmd and /l3/m1/odd_state (relates to unverified D3).' },
  { id: 'M2', name: 'World Model', sec: 6,
    spec: 'docs/Design/TDL-Kernel/M2-World-Model/M2-spec.md', progress: 'docs/Design/TDL-Kernel/M2-World-Model/M2-progress.md',
    node: 'src/l3_tdl_kernel/m2_world_model/src/world_model_node.cpp',
    pub: '/l3/m2/world_state, /l3/m2/threat_state',
    extra: 'M2 should compute CPA/TCPA + COLREG geometric pre-classification (encounter_classifier.cpp). KEY CHECK: does M2 actually compute & populate cpa_m/tcpa_s + encounter class in world_state, or is CPA/TCPA computed in the bridge instead (D4)? Does anyone consume /l3/m2/threat_state?' },
  { id: 'M3', name: 'Mission Manager', sec: 7,
    spec: 'docs/Design/TDL-Kernel/M3-Mission-Manager/M3-spec.md', progress: 'docs/Design/TDL-Kernel/M3-Mission-Manager/M3-progress.md',
    node: 'src/l3_tdl_kernel/m3_mission_manager/src/mission_manager_node.cpp',
    pub: '/l3/m3/mission_goal, /l3/m3/mission_state, /l3/m3/route_replan_request, /l3/m3/tor_request',
    extra: 'M3 consumes L2 /l2/planned_route + /l1/voyage_task. The route-return (回归航路) symptom depends on M3. Prior memory flagged task_validity stuck INVALID / current_target_wp = (0,0). VERIFY current behavior in code. ALSO read docs/Design/TDL-Kernel/M3-Mission-Manager/M3-gap-fix-plan.md and assess whether the planned fix is implemented.' },
  { id: 'M4', name: 'Behavior Arbiter', sec: 8,
    spec: 'docs/Design/TDL-Kernel/M4-Behavior-Arbiter/M4-spec.md', progress: 'docs/Design/TDL-Kernel/M4-Behavior-Arbiter/M4-progress.md',
    node: 'src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_arbiter_node.cpp',
    pub: '/l3/m4/behavior_plan, /l3/m4/reactive_override_cmd',
    extra: 'IvP multi-objective arbitration. IGNORE the .salvage-d3.1 copy ENTIRELY (stale). KEY CHECK: does M4 read M6 /l3/m6/colregs_constraint primary_preferred_direction to pick turn direction, or HARDCODE starboard? Does it use M6 numeric_value for avoidance magnitude? (live shows behavior_plan healthy at 20Hz — confirm the heading window is M6-derived not hardcoded).' },
  { id: 'M5', name: 'Tactical Planner', sec: 9,
    spec: 'docs/Design/TDL-Kernel/M5-Tactical-Planner/M5-spec.md', progress: 'docs/Design/TDL-Kernel/M5-Tactical-Planner/M5-progress.md',
    node: 'src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp AND src/l3_tdl_kernel/m5_tactical_planner/src/bc_mpc/bc_mpc_node.cpp',
    pub: '/l3/m5/avoidance_plan',
    extra: 'Mid-MPC (IPOPT/CasADi) + BC-MPC. D1 keystone (intermittent Restoration_Failed, cost_colreg≡0) is KNOWN — do NOT re-derive it. INSTEAD check: (a) is bc_mpc_node actually LAUNCHED & wired into the pipeline, or dead/stub? (b) live-observed: avoidance_plan WAYPOINTS have unpopulated confidence/rationale/schema_version/stamp — confirm in the publisher code which fields it sets vs leaves default. (c) is the geometric fallback correctly marked DEGRADED not VALID (D2)? (d) does M5 gate on /l3/m1 ODD state or /l3/checker/veto?' },
  { id: 'M6', name: 'COLREGs Reasoner', sec: 10,
    spec: 'docs/Design/TDL-Kernel/M6-COLREGs-Reasoner/M6-spec.md', progress: 'docs/Design/TDL-Kernel/M6-COLREGs-Reasoner/M6-progress.md',
    node: 'src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_reasoner_node.cpp',
    pub: '/l3/m6/colregs_constraint, /l3/m6/rule_assessment',
    extra: 'NOTE: A4000 has an UNCOMMITTED working-tree edit to this exact node file — audit the file AS-IS on disk. Rules 13-17 + RuleLatch. D5/D6 claimed fixed — VERIFY conflict_detected is role-derived (not a {7,8,14} filter) and RuleLatch release logic (Rule-16 past-and-clear). Does it populate primary_preferred_direction + numeric_value constraints that M4/M5 need?' },
  { id: 'M7', name: 'Safety Supervisor', sec: 11,
    spec: 'docs/Design/TDL-Kernel/M7-Safety-Supervisor/M7-spec.md', progress: 'docs/Design/TDL-Kernel/M7-Safety-Supervisor/M7-progress.md',
    node: 'src/l3_tdl_kernel/m7_safety_supervisor/src/safety_supervisor_node.cpp',
    pub: '/l3/m7/heartbeat, /l3/m7/safety_alert, /l3/checker/veto',
    extra: 'IGNORE sotif/.salvage-d3.3b. Doer-Checker (ADR-2: must be ~100x simpler, INDEPENDENT code path — no shared code/lib/data with the Doer). KEY CHECK (D3, unverified): is /l3/checker/veto a SYNCHRONOUS hard gate (L4 cannot execute an un-checked plan) or async advisory? Which of HC-1..HC-6 (watchdog/COLREG/CPA/ROT/actuator/diag) are REAL vs deferred-stub? Does M5 or the bridge actually gate on the veto?' },
  { id: 'M8', name: 'HMI/Transparency Bridge', sec: 12,
    spec: 'docs/Design/TDL-Kernel/M8-HMI-Transparency-Bridge/M8-spec.md', progress: 'docs/Design/TDL-Kernel/M8-HMI-Transparency-Bridge/M8-progress.md',
    node: 'src/l3_tdl_kernel/m8_hmi_transparency_bridge/src/hmi_transparency_bridge_node.cpp',
    pub: '/l3/m8/operator_state, /l3/m8/ui_state, /l3/m8/tor_request, /l3/sat/data',
    extra: 'M8 aggregates the CMM triplet (current_state/rationale/forecast+uncertainty) from every module into SAT-1/2/3 for the web HMI. KEY CHECK: does M8 actually aggregate REAL per-module rationale()/forecast(), or pass through empty/placeholder strings? Which modules does it subscribe to for SAT data? Cross-check with the frontend audit (does the HMI show fields M8 never fills?). Note /sil/m8_ui_state also exists (bridge-side duplicate).' },
]

// ---------- cross-cutting scopes ----------
const CROSSCUT = [
  { id: 'contracts', label: 'contracts+launch', prompt: `${preamble()}${CTX}

SCOPE: ROS2 CONTRACT & WIRING ground-truth.
- Read the .msg files in src/l3_tdl_kernel/l3_msgs/msg/ and src/l3_tdl_kernel/l3_external_msgs/msg/ (use Read; key ones: WorldState, BehaviorPlan, AvoidancePlan, AvoidanceWaypoint, COLREGsConstraint, Constraint, ModeCmd, ODDState, MissionState, RuleAssessment, M7Observability, SAT1/2/3Data).
- Read src/l3_tdl_kernel/launch/l3_pipeline.launch.py and src/l3_tdl_kernel/launch/l3_params.yaml.

TASKS:
1. From the launch file, list EVERY node launched, its package, and topic REMAPPINGS. Flag any node that is NOT one of M1-M8 (extra glue).
2. Build the declared topic registry: topic -> msg_type -> publisher(s) -> subscriber(s). Cross-check against the live list below; flag topics declared-but-not-live and live-but-not-declared.
3. CMM CONTRACT check: the project mandates EVERY message carry stamp + schema_version + confidence in [0,1] + rationale. For each key .msg, state whether these 4 fields EXIST in the definition. Flag missing ones as DESIGN_IMPL_DESYNC.
4. Any .msg defined but never used anywhere (DEAD_CODE).
Report via topic_io (the registry, one entry per topic with direction PRODUCE for publishers' view) and findings.

LIVE TOPIC LIST:
${LIVE_TOPICS}` },
  { id: 'bridge', label: 'sil-bridge', prompt: `${preamble()}${CTX}

SCOPE: the SIL "bridge" glue layer (NOT in the architecture diagram — a band-aid). Files: docker/sil_topic_bridge.py (PRIMARY), docker/mock_l2_publisher.py, docker/diagnostic_mock_publisher.py, docker/fsm_aggregator_node.py.
The architecture says M5 outputs (psi,u,ROT) DIRECTLY to L4 with no bridge.

TASKS:
1. Enumerate DECISION logic that leaked into the bridge but belongs in L3 (DCPA/TCPA computation, 60-deg heading clamp, avoidance latch/teardown, arming gates, dead-stick/open-loop constant output). Cite file:line. Category LEAKED_LOGIC.
2. Enumerate what the bridge MOCKS or synthesizes (mock_l2_publisher route/voyage, diagnostic mock, fsm aggregator). Does mock_l2 hardcode a scenario route? Category MOCK.
3. Topic remappings: which /sil/* <-> /fusion/* <-> /l3/* the bridge bridges (report via topic_io with direction REMAP).
4. Any open-loop / constant-output dead-stick gate (e.g. emits fixed rudder when avoidance_active and target=None).
5. Quantify D4: exactly what avoidance decision still lives here vs in L3.` },
  { id: 'orchestrator', label: 'orchestrator', prompt: `${preamble()}${CTX}

SCOPE: src/sil_orchestrator/ (FastAPI, listens :18000 https; foxglove bridge :18765). Files: main.py, routers/debug_routes.py, scenario_routes.py, scoring_routes.py, export_routes.py, asdr_routes.py, checker_verification.py, lifecycle_bridge.py, scenario_store.py, marzip_builder.py, selfcheck_routes.py, schema_routes.py, ops_routes.py.

TASKS:
1. Which REST/WS endpoints serve the web HMI, and what data each returns (report via topic_io / findings).
2. Where does it return MOCK / synthesized / hardcoded data instead of live kernel data? Category MOCK.
3. Lifecycle gaps: scenario start/stop/reset/configure flow — anything that wedges or no-ops.
4. Any endpoint the HMI is likely to call that returns stub/empty/404/NotImplemented. Category STUB.
5. design-impl deltas vs how the orchestrator is described in docs (DESIGN_IMPL_DESYNC).` },
  { id: 'frontend', label: 'web-hmi', prompt: `${preamble()}${CTX}

SCOPE: web/src/ (Vite + React/TSX). Dirs: api/, store/, hooks/, screens/, components/, map/, types/. Also read web/src/types/sil/ for the data contracts the HMI expects.

TASKS:
1. Map how the HMI gets backend data: foxglove WS topics? orchestrator REST? both? List the concrete data sources in api/ + hooks/ + store/. (topic_io: direction CONSUME)
2. Find HARDCODED / MOCK / placeholder data in the UI (fake values, demo constants, TODO) shown as if live. Category MOCK.
3. Find DISPLAY-WITHOUT-BACKEND (断流 at the display layer): UI fields/panels/screens that show data the backend never sends — cross-check against the live topic list. Category FLOW_GAP.
4. Find data the backend DOES send but the HMI ignores or mis-maps (wrong field/units). Category DESIGN_IMPL_DESYNC.
5. Assess vs M8-spec (SAT-1/2/3 transparency): does the HMI implement the SAT transparency surface or stub it?

LIVE TOPIC LIST (what backend actually publishes):
${LIVE_TOPICS}` },
  { id: 'sim', label: 'sim-workbench', prompt: `${preamble()}${CTX}

SCOPE: src/sim_workbench/ (simulator side) + docker mocks. Dirs: sil_nodes, fcb_simulator, fmi_bridge, ais_bridge, mock_publishers, scenario_authoring, shell_b_harness, sil_lifecycle, sil_common, ship_sim_interfaces.

TASKS:
1. Map the real-vs-mock boundary: what is genuinely simulated (ship dynamics RK4, environment disturbance, sensors/radar) vs what is a PLACEHOLDER pretending to be real. Category MOCK vs note REAL.
2. Find placeholders masquerading as real physics/sensors (hardcoded returns, constant outputs). Category MOCK with masquerade note.
3. How own_ship / target / environment get into the kernel: the /sil/* -> /fusion/* path (who maps it). topic_io REMAP/PRODUCE.
4. Is the FMI bridge / FCB simulator REAL or stub? Is the AIS bridge wired?
5. design-impl deltas / dead nodes not launched.` },
]

// ---------- flow segments ----------
const SEGMENTS = [
  { key: 'sense', name: 'Perception -> World Model', edges: `- Fusion -> M2: /fusion/own_ship_state, /fusion/tracked_targets, /fusion/environment_state
- M2 -> M1: /l3/m2/world_state
- M2 -> M4: /l3/m2/world_state
- M2 -> M5: /l3/m2/world_state (target trajectory for MPC)
- M2 -> M6: /l3/m2/world_state (target geometry for rules)
- M2 -> M7: /l3/m2/world_state
- M2 -> ? : /l3/m2/threat_state (who consumes?)` },
  { key: 'decide', name: 'Context + Rules -> Behavior', edges: `- M1 -> M4: /l3/m1/mode_cmd, /l3/m1/odd_state (HARD gate or soft read?)
- M1 -> M6: ODD params (how delivered?)
- M1 -> M5: speed/trajectory constraints (how delivered?)
- M6 -> M4: /l3/m6/colregs_constraint (consumed as HARD constraint?)
- M6 -> M5: /l3/m6/colregs_constraint + /l3/m6/rule_assessment
- M6 -> M7: /l3/m6/colregs_constraint
- M3 -> M1/M5/bridge: /l3/m3/mission_goal, /l3/m3/mission_state (route-return chain)
- L1/L2 -> M3: /l1/voyage_task, /l2/planned_route, /l2/speed_profile, /l2/replan_response` },
  { key: 'act', name: 'Planning -> Actuation + Safety', edges: `- M4 -> M5: /l3/m4/behavior_plan (heading/speed window as constraint)
- M5 -> M7: /l3/m5/avoidance_plan (for checking)
- M5 -> bridge -> L4: /l3/m5/avoidance_plan -> /sil/actuator_cmd (does bridge translate plan->actuator, or re-decide?)
- M7 -> M1: /l3/checker/veto, /l3/m7/safety_alert (synchronous gate?)
- M7 heartbeat /l3/m7/heartbeat -> who watches it?
- reflex/override: /l3/reflex/activation, /l3/override/active, /l3/m4/reactive_override_cmd` },
  { key: 'hmi', name: 'Kernel <-> Orchestrator <-> Web HMI', edges: `- Kernel topics -> orchestrator (foxglove WS :18765 / REST :18000) -> web HMI (:5173)
- M8 -> HMI: /l3/m8/ui_state, /l3/m8/operator_state, /l3/sat/data, /l3/m8/tor_request
- bridge <-> sim: /sil/own_ship_state, /sil/actuator_cmd, /sil/environment, /sil/bridge_state, /sil/module_pulse
- orchestrator REST endpoints the HMI polls (scenario/scoring/export/asdr) -> live or stub?` },
]

function modulePrompt(m) {
  return `${preamble()}${CTX}

YOUR SCOPE: ${m.id} — ${m.name}.
- Design spec (PRIMARY, read fully): ${m.spec}
- Progress doc (claims of what's DONE — compare vs reality to catch desync & overclaims): ${m.progress}
- Architecture report section (read only if cross-module context needed): docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md (M${m.id.slice(1)} ≈ §${m.sec})
- Implementation entrypoint(s): ${m.node}
- Topics this module is EXPECTED to publish: ${m.pub}
${m.extra}

TASKS:
A. Map this module's ACTUAL ROS2 subscriptions & publications from the node code. For each PUBLICATION, check whether all message fields (esp. CMM-mandated stamp/schema_version/confidence/rationale) are POPULATED or left default/zero.
B. For each design responsibility in the spec, classify impl status REAL/PARTIAL/STUB/MOCK/MISSING with file:line.
C. design_impl_deltas: spec says X, code does Y, with severity.
D. mocks_stubs: list stubs/mocks and whether they masquerade as real (hardcoded return / TODO / fallback dressed as nominal).
E. progress_doc_overclaims: progress.md claims done but code says otherwise.
F. flow_gaps_suspected: this module expects input X that nobody publishes, or publishes Y that nobody consumes.

Return the structured object.`
}

function flowPrompt(seg, consolidated) {
  return `${preamble()}

FLOW-GAP (断流) DETECTOR. Phase-1 agents already mapped each module's pub/sub & deltas (consolidated below). For each edge in SEGMENT "${seg.name}", give a verdict by checking the ACTUAL code on BOTH ends (does the publisher populate the fields? does the subscriber actually READ & USE them, not just subscribe-and-drop?) and cross-checking the LIVE topic list. Use codegraph to confirm the subscriber callback consumes the field.

EDGES TO ADJUDICATE:
${seg.edges}

CONSOLIDATED PHASE-1 MAPS (compact):
${consolidated}

LIVE TOPICS (actually publishing on A4000 now):
${LIVE_TOPICS}

For each edge: verdict in {CONNECTED, BROKEN_NO_SUBSCRIBER, BROKEN_NO_PUBLISHER, MOCK_INTERCEPTED, SCHEMA_MISMATCH, FIELD_UNPOPULATED, PARTIAL}. CONNECTED requires publisher emits populated fields AND subscriber reads+uses it. Cite file:line. Fill live_cross_check (is the topic in the live list).`
}

function verifyPrompt(c) {
  return `${preamble()}

ADVERSARIAL VERIFICATION. A Phase-1/2 audit agent made the claim below. Re-read the CURRENT code (NOT salvage/archive) at the cited location and determine the truth. A previous audit of THIS codebase was burned by reading stale .salvage backups + 5-day-old memory and reported 3 already-fixed issues as open — so be skeptical, but do not dismiss genuine gaps.

CLAIM (source ${c.src}, severity ${c.severity}):
  Title: ${c.title}
  Detail: ${c.claim}
  Cited evidence: ${c.evidence}

Verdict:
- CONFIRMED: current code clearly shows this gap (quote the exact current line(s)).
- STALE_ALREADY_FIXED: cited code no longer matches; current code already handles it (quote what's there now).
- REFUTED: claim is wrong/misread even in current code.
- PARTIALLY_CONFIRMED: real but over/understated.
Always quote the current code you actually read. Set confidence HIGH/MEDIUM/LOW.`
}

function trim(phase1) {
  const mods = phase1.filter(x => x && x.kind === 'module').map(x => ({
    module: x.id,
    subs: (x.data.subscriptions || []).map(s => `${s.topic}[${s.status}]`),
    pubs: (x.data.publications || []).map(p => `${p.topic}[${p.status}${p.unpopulated_fields && p.unpopulated_fields.length ? ' UNPOP:' + p.unpopulated_fields.join(',') : ''}]`),
    deltas: (x.data.design_impl_deltas || []).map(d => `${d.severity}:${d.title}`),
    gaps: (x.data.flow_gaps_suspected || []).map(g => g.edge || g.symptom),
  }))
  const cross = phase1.filter(x => x && x.kind === 'cross').map(x => ({
    area: x.id,
    topic_io: (x.data.topic_io || []).map(t => `${t.direction}:${t.topic}`),
    findings: (x.data.findings || []).map(f => `${f.severity}/${f.category}:${f.title}`),
  }))
  return JSON.stringify({ modules: mods, cross }, null, 1)
}

function collectCandidates(phase1, flow) {
  const out = []
  phase1.forEach(x => {
    if (!x || !x.data) return
    if (x.kind === 'module') {
      (x.data.design_impl_deltas || []).filter(d => d.severity === 'CRITICAL' || d.severity === 'HIGH').forEach(d =>
        out.push({ src: x.id, title: d.title, claim: `design: ${d.design_says} | impl: ${d.impl_does}`, evidence: d.evidence, severity: d.severity }))
      ;(x.data.mocks_stubs || []).filter(s => s.masquerades_as_real).forEach(s =>
        out.push({ src: x.id, title: 'MOCK masquerading: ' + s.what, claim: s.note || s.what, evidence: s.file_line, severity: 'HIGH' }))
      ;(x.data.progress_doc_overclaims || []).forEach(o =>
        out.push({ src: x.id, title: 'Progress overclaim: ' + o.claim, claim: o.reality, evidence: o.evidence, severity: 'MEDIUM' }))
    } else {
      (x.data.findings || []).filter(f => f.severity === 'CRITICAL' || f.severity === 'HIGH').forEach(f =>
        out.push({ src: x.id, title: f.title, claim: f.detail || '', evidence: f.evidence, severity: f.severity }))
    }
  })
  flow.forEach(s => {
    if (!s || !s.data) return
    ;(s.data.edges || []).filter(e => e.verdict !== 'CONNECTED' && (e.severity === 'CRITICAL' || e.severity === 'HIGH')).forEach(e =>
      out.push({ src: 'FLOW:' + (s.data.segment || s.id), title: `${e.edge} = ${e.verdict}`, claim: e.detail || '', evidence: e.evidence, severity: e.severity }))
  })
  return out
}

// ---------- run ----------
phase('Map')
const moduleThunks = MODULES.map(m => () => agent(modulePrompt(m), { model: 'sonnet', phase: 'Map', label: `map:${m.id}`, schema: MODULE_SCHEMA }).then(d => ({ kind: 'module', id: m.id, data: d })))
const crossThunks = CROSSCUT.map(c => () => agent(c.prompt, { model: 'sonnet', phase: 'Map', label: `map:${c.id}`, schema: GENERIC_SCHEMA }).then(d => ({ kind: 'cross', id: c.id, data: d })))
const phase1 = (await parallel([...moduleThunks, ...crossThunks])).filter(Boolean)
log(`Phase 1 (Map) done: ${phase1.length}/${MODULES.length + CROSSCUT.length} maps returned`)

phase('Flow')
const consolidated = trim(phase1)
const flow = (await parallel(SEGMENTS.map(s => () => agent(flowPrompt(s, consolidated), { model: 'sonnet', phase: 'Flow', label: `flow:${s.key}` , schema: FLOW_SCHEMA }).then(d => ({ kind: 'flow', id: s.key, data: d }))))).filter(Boolean)
log(`Phase 2 (Flow) done: ${flow.length}/${SEGMENTS.length} segments`)

phase('Verify')
let candidates = collectCandidates(phase1, flow)
const RANK = { CRITICAL: 0, HIGH: 1, MEDIUM: 2, LOW: 3 }
candidates.sort((a, b) => (RANK[a.severity] === undefined ? 9 : RANK[a.severity]) - (RANK[b.severity] === undefined ? 9 : RANK[b.severity]))
const CAP = 64
const toVerify = candidates.slice(0, CAP)
if (candidates.length > CAP) log(`NOTE: verifying top ${CAP}/${candidates.length} findings by severity; ${candidates.length - CAP} lower-severity findings returned UNVERIFIED (not silently dropped).`)
log(`Verifying ${toVerify.length} findings adversarially`)
const verified = (await parallel(toVerify.map(c => () => agent(verifyPrompt(c), { model: 'sonnet', phase: 'Verify', label: `verify:${c.src}` , schema: VERDICT_SCHEMA }).then(v => ({ ...c, verdict: v }))))).filter(Boolean)

const critic = await agent(`${preamble()}

COMPLETENESS CRITIC for a M1-M8 + front/back-end gap audit. Given all findings below, identify what was NOT covered: any module boundary, mock, topic, design responsibility, or gap category that no agent examined, and any obviously-missed area. Be specific and actionable. Use category OTHER for coverage gaps.

CONSOLIDATED MAPS:
${trim(phase1)}

FLOW VERDICTS:
${JSON.stringify(flow.map(f => f.data), null, 1)}`, { model: 'sonnet', phase: 'Verify', label: 'completeness-critic', schema: GENERIC_SCHEMA })

return {
  phase1, flow, verified,
  unverified_count: candidates.length - toVerify.length,
  critic,
  stats: {
    modules: phase1.filter(x => x.kind === 'module').length,
    cross: phase1.filter(x => x.kind === 'cross').length,
    flow: flow.length,
    candidates: candidates.length,
    verified: verified.length,
  },
}
