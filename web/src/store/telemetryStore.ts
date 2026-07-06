import { create } from 'zustand';
import type { OwnShipState, TargetVesselState, EnvironmentState, ModulePulse } from '../types';
import type { SAT2Data, SAT3Data, SotifMetrics } from '../types/sat';

export interface NormalizedWaypoint {
  lat: number;
  lon: number;
}

export interface VoyagePlanData {
  waypoints: NormalizedWaypoint[];
  cruiseSpeed: number;
  source: 'static_yaml' | 'l2_realtime';
  speedProfileKn?: number[];
  totalDistanceNm?: number;
  estimatedDurationS?: number;
  routeId?: number;
}

export type AvoidanceSegmentSource =
  | 'MID_MPC_OPTIMIZED'
  | 'MID_MPC_TERMINAL_HOLD'
  | 'REJOIN_TO_L2'
  | 'L2_NOMINAL_SUFFIX'
  | 'DEGRADED_CORRIDOR'
  | 'UNKNOWN';

export interface AvoidanceWaypointData extends NormalizedWaypoint {
  confidence?: number;
  targetSpeedKn?: number;
  targetSpeedMps?: number;
  turnRadiusM?: number;
  rationale?: string;
  segmentSource?: AvoidanceSegmentSource;
}

export interface AvoidancePlanData {
  waypoints: AvoidanceWaypointData[];
  horizonS?: number;
  status?: string;
  confidence?: number;
  rationale?: string;
  routeHash?: number;
  nlpSolverStatus?: number;
  nlpKktResidual?: number;
  nlpTailGateFailed?: boolean;
}

export interface OddStateData {
  zone?: number;
  autoLevel?: number;
  health?: number;
  envelopeState?: number;
  conformanceScore?: number;
  tmrS?: number;
  tdlS?: number;
  confidence?: number;
  rationale?: string;
}

export interface BehaviorPlanData {
  behavior?: number;
  headingMinDeg?: number;
  headingMaxDeg?: number;
  speedMinKn?: number;
  speedMaxKn?: number;
  confidence?: number;
  rationale?: string;
}

export interface ColregsConstraintData {
  ruleId?: number;
  phase?: string;
  role?: number;
  preferredDirection?: string;
  minAlterationDeg?: number;
  conflictDetected?: boolean;
  confidence?: number;
  rationale?: string;
}

export interface SafetyAlertData {
  alertType?: number;
  severity?: number;
  description?: string;
  recommendedMrm?: string;
  confidence?: number;
  rationale?: string;
}

export type ThreatRiskPhase = 'Clear' | 'Monitor' | 'Warning' | 'Danger' | 'Critical' | 'Unknown';

export interface ThreatDomainAxesData {
  forwardM: number;
  asternM: number;
  starboardM: number;
  portM: number;
}

export interface ThreatRiskTargetData {
  targetId: string;
  riskPhase: ThreatRiskPhase;
  riskScore: number;
  primary: boolean;
  rangeM?: number;
  dcpaM?: number;
  tcpaS?: number;
  warningMarginM?: number;
  dangerMarginM?: number;
  closingSpeedMps?: number;
  relativeBearingDeg?: number;
  colregsDuty?: string;
  tdvWarningS?: number;
  tdvDangerS?: number;
}

export interface ThreatRiskStateData {
  stampS?: number;
  targets: ThreatRiskTargetData[];
  primaryTargetId?: string;
  dangerAxes: ThreatDomainAxesData;
  warningAxes: ThreatDomainAxesData;
  superellipsePower?: number;
  actionHorizonS?: number;
  criticalHorizonS?: number;
  confidence?: number;
  rationale?: string;
}

export interface ThreatRiskHistorySample {
  t: number;
  primaryTargetId?: string;
  riskPhase: ThreatRiskPhase;
  riskScore: number;
  warningMarginM?: number;
  dangerMarginM?: number;
  dcpaM?: number;
  tcpaS?: number;
}

// ------------------------------------------------------------------
// Lightweight local types for topics not yet in generated Protobuf TS
// ------------------------------------------------------------------
export interface ASDREvent {
  stamp?: { seconds?: number };
  event_type: string;
  rule_ref?: string;
  decision_id?: string;
  verdict?: 0 | 1 | 2 | 3; // UNKNOWN / PASS / RISK / FAIL
  payload_json?: string;
}

export interface LifecycleStatus {
  current_state?: number; // 0=UNKNOWN 1=UNCONFIGURED 2=INACTIVE 3=ACTIVE 4=DEACTIVATING 5=FINALIZED
  scenario_id?: string;
  scenario_hash?: string;
  sim_time?: number;
  wall_time?: number;
  sim_rate?: number;
}

export interface SensorState {
  id: string;
  state: 'ok' | 'warning' | 'fail' | 'disabled';
  lastUpdate: number;
  payload?: any;
}

export interface CommLinkState {
  id: string;
  state: 'ok' | 'warning' | 'fail' | 'disabled';
  lastUpdate: number;
  payload?: any;
}

export interface FaultState {
  fault_id: string;
  type: string;
  active: boolean;
  injectedAt: number;
  duration: number;
}

export interface ControlCmdState {
  rudder: number;
  throttle: number;
  rpm: number;
  pitch: number;
  history: Array<{ rudder: number; throttle: number; rpm: number; pitch: number; t: number }>;
}

const MAX_ASDR = 200;
const MAX_TRAIL = 600; // 600 points × 50Hz = 12 s of trail
const MAX_THREAT_HISTORY = 240;

const STALE_MS = 3_000;

interface TelemetryState {
  // Core telemetry
  ownShip: OwnShipState | null;
  targets: TargetVesselState[];
  environment: EnvironmentState | null;
  modulePulses: ModulePulse[];
  // New in DEMO-1 revision
  asdrEvents: ASDREvent[];
  lifecycleStatus: LifecycleStatus | null;
  wsConnected: boolean;
  /** [lon, lat] pairs for own-ship trajectory trail */
  ownShipTrail: [number, number][];
  lastTrailTime: number;
  /** [lon, lat] pairs for target ship trajectory trails, keyed by target ID/MMSI */
  targetTrails: Record<string, [number, number][]>;
  targetLastTrailTimes: Record<string, number>;
  /** Real-time 6-dim scoring row from /sil/scoring_row @ 1Hz */
  scoringRow: any;
  /** Sensor health (8 sensors) */
  sensors: SensorState[];
  /** Comm-link health (6 links) */
  commLinks: CommLinkState[];
  /** Active fault list */
  faultStatus: FaultState[];
  /** Latest control command from M5/L4 */
  controlCmd: ControlCmdState | null;
  /** Preflight log ring buffer */
  preflightLog: Array<{ timestamp: string; level: string; message: string }>;
  sat2: SAT2Data | null;
  sat3: SAT3Data | null;
  sotifMetrics: SotifMetrics | null;
  sat2LastReceivedAt: number | null;
  sat3LastReceivedAt: number | null;
  sotifMetricsLastReceivedAt: number | null;
  isSat2Stale: () => boolean;
  isSat3Stale: () => boolean;
  isSotifMetricsStale: () => boolean;
  voyagePlan: VoyagePlanData | null;
  avoidancePlan: AvoidancePlanData | null;
  oddState: OddStateData | null;
  behaviorPlan: BehaviorPlanData | null;
  colregsConstraint: ColregsConstraintData | null;
  safetyAlert: SafetyAlertData | null;
  threatState: ThreatRiskStateData | null;
  threatRiskHistory: ThreatRiskHistorySample[];

  updateOwnShip: (state: OwnShipState) => void;
  updateTargets: (targets: TargetVesselState[]) => void;
  clearTargets: () => void;
  updateEnvironment: (env: EnvironmentState) => void;
  updateModulePulses: (pulses: ModulePulse[]) => void;
  appendAsdrEvent: (evt: ASDREvent) => void;
  updateLifecycleStatus: (status: LifecycleStatus) => void;
  setWsConnected: (v: boolean) => void;
  updateScoringRow: (row: any) => void;
  updateSensors: (sensors: SensorState[]) => void;
  updateCommLinks: (links: CommLinkState[]) => void;
  updateFaultStatus: (faults: FaultState[]) => void;
  updateControlCmd: (cmd: ControlCmdState) => void;
  appendPreflightLog: (entry: { timestamp: string; level: string; message: string }) => void;
  updateSat2: (data: SAT2Data) => void;
  updateSat3: (data: SAT3Data) => void;
  updateSotifMetrics: (metrics: SotifMetrics) => void;
  updateVoyagePlan: (plan: VoyagePlanData | null) => void;
  updateAvoidancePlan: (plan: any | null) => void;
  updateOddState: (state: any | null) => void;
  updateBehaviorPlan: (plan: any | null) => void;
  updateColregsConstraint: (constraint: any | null) => void;
  updateSafetyAlert: (alert: any | null) => void;
  updateThreatState: (state: any | null) => void;
  reset: () => void;
}

const initialState = {
  ownShip: null,
  targets: [],
  environment: null,
  modulePulses: [],
  asdrEvents: [],
  lifecycleStatus: null,
  wsConnected: false,
  ownShipTrail: [] as [number, number][],
  targetTrails: {} as Record<string, [number, number][]>,
  targetLastTrailTimes: {} as Record<string, number>,
  scoringRow: null,
  sensors: [],
  commLinks: [],
  faultStatus: [],
  controlCmd: null,
  preflightLog: [],
  sat2: null,
  sat3: null,
  sotifMetrics: null,
  sat2LastReceivedAt: null,
  sat3LastReceivedAt: null,
  sotifMetricsLastReceivedAt: null,
  lastTrailTime: 0,
  voyagePlan: null,
  avoidancePlan: null,
  oddState: null,
  behaviorPlan: null,
  colregsConstraint: null,
  safetyAlert: null,
  threatState: null,
  threatRiskHistory: [],
};

function numberOrUndefined(value: unknown): number | undefined {
  return typeof value === 'number' && Number.isFinite(value) ? value : undefined;
}


function segmentSourceLabel(value: unknown): AvoidanceSegmentSource {
  switch (value) {
    case 0: return 'MID_MPC_OPTIMIZED';
    case 1: return 'MID_MPC_TERMINAL_HOLD';
    case 2: return 'REJOIN_TO_L2';
    case 3: return 'L2_NOMINAL_SUFFIX';
    case 4: return 'DEGRADED_CORRIDOR';
    case 'MID_MPC_OPTIMIZED':
    case 'MID_MPC_TERMINAL_HOLD':
    case 'REJOIN_TO_L2':
    case 'L2_NOMINAL_SUFFIX':
    case 'DEGRADED_CORRIDOR':
      return value;
    default:
      return 'UNKNOWN';
  }
}

function normalizeAvoidancePlan(plan: any | null): AvoidancePlanData | null {
  if (!plan) return null;

  const routeLatitudes = Array.isArray(plan.latitude) ? plan.latitude : [];
  const routeLongitudes = Array.isArray(plan.longitude) ? plan.longitude : [];
  const routeSpeeds = Array.isArray(plan.command_speed_mps ?? plan.commandSpeedMps)
    ? (plan.command_speed_mps ?? plan.commandSpeedMps)
    : [];
  const segmentSources = Array.isArray(plan.segment_source ?? plan.segmentSource)
    ? (plan.segment_source ?? plan.segmentSource)
    : [];

  const routeWaypoints = routeLatitudes.flatMap((latRaw: unknown, index: number) => {
    const lat = numberOrUndefined(latRaw);
    const lon = numberOrUndefined(routeLongitudes[index]);
    if (lat === undefined || lon === undefined) return [];
    return [{
      lat,
      lon,
      targetSpeedMps: numberOrUndefined(routeSpeeds[index]),
      segmentSource: segmentSourceLabel(segmentSources[index]),
    }];
  });

  const richWaypoints = Array.isArray(plan.waypoints)
    ? plan.waypoints.flatMap((wp: any, index: number) => {
      const pos = wp?.position ?? wp;
      const lat = numberOrUndefined(pos?.latitude ?? pos?.lat);
      const lon = numberOrUndefined(pos?.longitude ?? pos?.lon);
      if (lat === undefined || lon === undefined) return [];

      return [{
        lat,
        lon,
        confidence: numberOrUndefined(wp?.confidence),
        targetSpeedKn: numberOrUndefined(wp?.target_speed_kn ?? wp?.targetSpeedKn),
        turnRadiusM: numberOrUndefined(wp?.turn_radius_m ?? wp?.turnRadiusM),
        rationale: typeof wp?.rationale === 'string' ? wp.rationale : undefined,
        segmentSource: segmentSourceLabel(segmentSources[index]),
      }];
    })
    : [];

  const waypoints = routeWaypoints.length > 0 ? routeWaypoints : richWaypoints;

  return {
    waypoints,
    horizonS: numberOrUndefined(plan.horizon_s ?? plan.horizonS),
    status: typeof plan.status === 'string' ? plan.status : undefined,
    confidence: numberOrUndefined(plan.confidence),
    rationale: typeof plan.rationale === 'string' ? plan.rationale : undefined,
    routeHash: numberOrUndefined(plan.route_hash ?? plan.routeHash),
    nlpSolverStatus: numberOrUndefined(plan.nlp_solver_status ?? plan.nlpSolverStatus),
    nlpKktResidual: numberOrUndefined(plan.nlp_kkt_residual ?? plan.nlpKktResidual),
    nlpTailGateFailed: typeof (plan.nlp_tail_gate_failed ?? plan.nlpTailGateFailed) === 'boolean'
      ? (plan.nlp_tail_gate_failed ?? plan.nlpTailGateFailed)
      : undefined,
  };
}

function normalizeOddState(state: any | null): OddStateData | null {
  if (!state) return null;
  return {
    zone: numberOrUndefined(state.current_zone ?? state.currentZone ?? state.zone),
    autoLevel: numberOrUndefined(state.auto_level ?? state.autoLevel),
    health: numberOrUndefined(state.health),
    envelopeState: numberOrUndefined(state.envelope_state ?? state.envelopeState),
    conformanceScore: numberOrUndefined(state.conformance_score ?? state.conformanceScore),
    tmrS: numberOrUndefined(state.tmr_s ?? state.tmrS),
    tdlS: numberOrUndefined(state.tdl_s ?? state.tdlS),
    confidence: numberOrUndefined(state.confidence),
    rationale: typeof state.rationale === 'string' ? state.rationale : undefined,
  };
}

function normalizeBehaviorPlan(plan: any | null): BehaviorPlanData | null {
  if (!plan) return null;
  return {
    behavior: numberOrUndefined(plan.behavior),
    headingMinDeg: numberOrUndefined(plan.heading_min_deg ?? plan.headingMinDeg),
    headingMaxDeg: numberOrUndefined(plan.heading_max_deg ?? plan.headingMaxDeg),
    speedMinKn: numberOrUndefined(plan.speed_min_kn ?? plan.speedMinKn),
    speedMaxKn: numberOrUndefined(plan.speed_max_kn ?? plan.speedMaxKn),
    confidence: numberOrUndefined(plan.confidence),
    rationale: typeof plan.rationale === 'string' ? plan.rationale : undefined,
  };
}

function normalizeColregsConstraint(constraint: any | null): ColregsConstraintData | null {
  if (!constraint) return null;
  const activeRules = Array.isArray(constraint.active_rules)
    ? constraint.active_rules
    : Array.isArray(constraint.activeRules)
      ? constraint.activeRules
      : [];
  const encounterRulePriority = [14, 15, 13, 17, 16, 18, 8];
  const primaryRule = encounterRulePriority
    .map((ruleId) => activeRules.find((rule: any) => (rule.rule_id ?? rule.ruleId) === ruleId))
    .find((rule) => rule !== undefined)
    ?? activeRules.find((rule: any) => (
      (rule.preferred_direction ?? rule.preferredDirection) !== 'HOLD'
      || (rule.min_alteration_deg ?? rule.minAlterationDeg ?? 0) > 0
    ))
    ?? activeRules[0]
    ?? {};
  return {
    ruleId: numberOrUndefined(constraint.rule_id ?? constraint.ruleId ?? primaryRule.rule_id ?? primaryRule.ruleId),
    phase: typeof constraint.phase === 'string' ? constraint.phase : undefined,
    role: numberOrUndefined(constraint.primary_role ?? constraint.primaryRole ?? constraint.role ?? primaryRule.role),
    preferredDirection: typeof (constraint.primary_preferred_direction ?? constraint.primaryPreferredDirection ?? primaryRule.preferred_direction ?? primaryRule.preferredDirection) === 'string'
      ? (constraint.primary_preferred_direction ?? constraint.primaryPreferredDirection ?? primaryRule.preferred_direction ?? primaryRule.preferredDirection)
      : undefined,
    minAlterationDeg: numberOrUndefined(constraint.min_alteration_deg ?? constraint.minAlterationDeg ?? primaryRule.min_alteration_deg ?? primaryRule.minAlterationDeg),
    conflictDetected: typeof (constraint.conflict_detected ?? constraint.conflictDetected) === 'boolean'
      ? (constraint.conflict_detected ?? constraint.conflictDetected)
      : undefined,
    confidence: numberOrUndefined(constraint.confidence ?? primaryRule.confidence ?? primaryRule.rule_confidence ?? primaryRule.ruleConfidence),
    rationale: typeof constraint.rationale === 'string'
      ? constraint.rationale
      : typeof primaryRule.rationale === 'string'
        ? primaryRule.rationale
        : undefined,
  };
}

function normalizeSafetyAlert(alert: any | null): SafetyAlertData | null {
  if (!alert) return null;
  return {
    alertType: numberOrUndefined(alert.alert_type ?? alert.alertType),
    severity: numberOrUndefined(alert.severity),
    description: typeof alert.description === 'string' ? alert.description : undefined,
    recommendedMrm: typeof (alert.recommended_mrm ?? alert.recommendedMrm) === 'string'
      ? (alert.recommended_mrm ?? alert.recommendedMrm)
      : undefined,
    confidence: numberOrUndefined(alert.confidence),
    rationale: typeof alert.rationale === 'string' ? alert.rationale : undefined,
  };
}

function normalizeRiskPhase(value: unknown): ThreatRiskPhase {
  if (value === 'Clear' || value === 'Monitor' || value === 'Warning' || value === 'Danger' || value === 'Critical') {
    return value;
  }
  return 'Unknown';
}

function arrayValue<T>(values: unknown, index: number): T | undefined {
  return Array.isArray(values) ? values[index] as T | undefined : undefined;
}

function timestampSeconds(stamp: any): number | undefined {
  const sec = numberOrUndefined(stamp?.sec ?? stamp?.seconds);
  const nsec = numberOrUndefined(stamp?.nanosec ?? stamp?.nanos);
  if (sec === undefined) return undefined;
  return sec + (nsec ?? 0) / 1_000_000_000;
}

function normalizeThreatState(state: any | null): ThreatRiskStateData | null {
  if (!state) return null;

  const ids = Array.isArray(state.target_ids) ? state.target_ids : Array.isArray(state.targetIds) ? state.targetIds : [];
  const targets: ThreatRiskTargetData[] = ids.map((rawId: unknown, idx: number) => ({
    targetId: String(rawId),
    riskPhase: normalizeRiskPhase(arrayValue(state.risk_phases ?? state.riskPhases, idx)),
    riskScore: numberOrUndefined(arrayValue(state.risk_scores ?? state.riskScores, idx)) ?? 0,
    primary: Boolean(arrayValue(state.primary_flags ?? state.primaryFlags, idx)),
    rangeM: numberOrUndefined(arrayValue(state.range_m ?? state.rangeM, idx)),
    dcpaM: numberOrUndefined(arrayValue(state.dcpa_m ?? state.dcpaM, idx)),
    tcpaS: numberOrUndefined(arrayValue(state.tcpa_s ?? state.tcpaS, idx)),
    warningMarginM: numberOrUndefined(arrayValue(state.warning_margin_m ?? state.warningMarginM, idx)),
    dangerMarginM: numberOrUndefined(arrayValue(state.danger_margin_m ?? state.dangerMarginM, idx)),
    closingSpeedMps: numberOrUndefined(arrayValue(state.closing_speed_mps ?? state.closingSpeedMps, idx)),
    relativeBearingDeg: numberOrUndefined(arrayValue(state.relative_bearing_deg ?? state.relativeBearingDeg, idx)),
    colregsDuty: typeof arrayValue(state.colregs_duties ?? state.colregsDuties, idx) === 'string'
      ? arrayValue<string>(state.colregs_duties ?? state.colregsDuties, idx)
      : undefined,
    tdvWarningS: numberOrUndefined(arrayValue(state.tdv_warning_s ?? state.tdvWarningS, idx)),
    tdvDangerS: numberOrUndefined(arrayValue(state.tdv_danger_s ?? state.tdvDangerS, idx)),
  }));

  const primary = targets.find((target) => target.primary) ?? targets[0];
  return {
    stampS: timestampSeconds(state.stamp),
    targets,
    primaryTargetId: primary?.targetId,
    dangerAxes: {
      forwardM: numberOrUndefined(state.danger_forward_m ?? state.dangerForwardM) ?? 0,
      asternM: numberOrUndefined(state.danger_astern_m ?? state.dangerAsternM) ?? 0,
      starboardM: numberOrUndefined(state.danger_starboard_m ?? state.dangerStarboardM) ?? 0,
      portM: numberOrUndefined(state.danger_port_m ?? state.dangerPortM) ?? 0,
    },
    warningAxes: {
      forwardM: numberOrUndefined(state.warning_forward_m ?? state.warningForwardM) ?? 0,
      asternM: numberOrUndefined(state.warning_astern_m ?? state.warningAsternM) ?? 0,
      starboardM: numberOrUndefined(state.warning_starboard_m ?? state.warningStarboardM) ?? 0,
      portM: numberOrUndefined(state.warning_port_m ?? state.warningPortM) ?? 0,
    },
    superellipsePower: numberOrUndefined(state.superellipse_power ?? state.superellipsePower),
    actionHorizonS: numberOrUndefined(state.action_horizon_s ?? state.actionHorizonS),
    criticalHorizonS: numberOrUndefined(state.critical_horizon_s ?? state.criticalHorizonS),
    confidence: numberOrUndefined(state.confidence),
    rationale: typeof state.rationale === 'string' ? state.rationale : undefined,
  };
}

export const useTelemetryStore = create<TelemetryState>((set, get) => ({
  ...initialState,
  isSat2Stale: () => {
    const t = get().sat2LastReceivedAt;
    return t === null || Date.now() - t > STALE_MS;
  },
  isSat3Stale: () => {
    const t = get().sat3LastReceivedAt;
    return t === null || Date.now() - t > STALE_MS;
  },
  isSotifMetricsStale: () => {
    const t = get().sotifMetricsLastReceivedAt;
    return t === null || Date.now() - t > STALE_MS;
  },
  updateOwnShip: (ownShip) => set((s) => {
    // Append to trail if coordinates are valid
    const lon = ownShip.pose?.lon;
    const lat = ownShip.pose?.lat;
    if (typeof lon === 'number' && typeof lat === 'number') {
      const now = Date.now();
      if (s.ownShipTrail.length === 0 || now - s.lastTrailTime >= 1000) {
        const trail = [...s.ownShipTrail, [lon, lat] as [number, number]];
        return {
          ownShip,
          ownShipTrail: trail.length > MAX_TRAIL ? trail.slice(-MAX_TRAIL) : trail,
          lastTrailTime: now,
        };
      }
      return { ownShip };
    }
    // Keep ownShip null when decoded proto has no valid pose coordinates
    return {};
  }),
  updateTargets: (newTargets) => set((s) => {
    const merged = [...s.targets];
    const trails = { ...s.targetTrails };
    const lastTrailTimes = { ...s.targetLastTrailTimes };
    const now = Date.now();

    for (const nt of newTargets) {
      const idx = merged.findIndex((t) => t.mmsi === nt.mmsi);
      if (idx >= 0) {
        const existing = merged[idx] as any;
        const incoming = nt as any;
        merged[idx] = {
          ...existing,
          ...incoming,
          pose: {
            ...(existing.pose ?? {}),
            ...(incoming.pose ?? {}),
          },
          kinematics: {
            ...(existing.kinematics ?? {}),
            ...(incoming.kinematics ?? {}),
          },
        } as any;
      } else {
        merged.push(nt);
      }

      const id = nt.mmsi != null ? String(nt.mmsi) : null;
      if (id) {
        const lon = nt.pose?.lon;
        const lat = nt.pose?.lat;
        if (typeof lon === 'number' && typeof lat === 'number') {
          const lastTime = lastTrailTimes[id] ?? 0;
          if (!trails[id] || trails[id].length === 0 || now - lastTime >= 1000) {
            const currentTrail = trails[id] ?? [];
            const newTrail = [...currentTrail, [lon, lat] as [number, number]];
            trails[id] = newTrail.length > MAX_TRAIL ? newTrail.slice(-MAX_TRAIL) : newTrail;
            lastTrailTimes[id] = now;
          }
        }
      }
    }
    return {
      targets: merged,
      targetTrails: trails,
      targetLastTrailTimes: lastTrailTimes,
    };
  }),
  clearTargets: () => set({
    targets: [],
    targetTrails: {},
    targetLastTrailTimes: {},
  }),
  updateEnvironment: (environment) => set({ environment }),
  updateModulePulses: (modulePulses) => set({ modulePulses }),
  appendAsdrEvent: (evt) => set((s) => ({
    asdrEvents: s.asdrEvents.length >= MAX_ASDR
      ? [...s.asdrEvents.slice(1), evt]
      : [...s.asdrEvents, evt],
  })),
  updateLifecycleStatus: (lifecycleStatus) => set({ lifecycleStatus }),
  setWsConnected: (wsConnected) => set({ wsConnected }),
  updateScoringRow: (scoringRow) => set({ scoringRow }),
  updateSensors: (sensors) => set({ sensors }),
  updateCommLinks: (commLinks) => set({ commLinks }),
  updateFaultStatus: (faultStatus) => set({ faultStatus }),
  updateControlCmd: (controlCmd) => set((s) => {
    const history = s.controlCmd?.history ?? [];
    const entry = {
      rudder: controlCmd.rudder,
      throttle: controlCmd.throttle,
      rpm: controlCmd.rpm,
      pitch: controlCmd.pitch,
      t: Date.now(),
    };
    return {
      controlCmd: { ...controlCmd, history: [...history, entry].slice(-600) },
    };
  }),
  appendPreflightLog: (entry) => set((s) => ({
    preflightLog: [...s.preflightLog, entry].slice(-1000),
  })),
  updateSat2: (sat2) => set({ sat2, sat2LastReceivedAt: Date.now() }),
  updateSat3: (sat3) => set({ sat3, sat3LastReceivedAt: Date.now() }),
  updateSotifMetrics: (sotifMetrics) => set({ sotifMetrics, sotifMetricsLastReceivedAt: Date.now() }),
  updateVoyagePlan: (voyagePlan) => set({ voyagePlan }),
  updateAvoidancePlan: (plan) => set({ avoidancePlan: normalizeAvoidancePlan(plan) }),
  updateOddState: (state) => set({ oddState: normalizeOddState(state) }),
  updateBehaviorPlan: (plan) => set({ behaviorPlan: normalizeBehaviorPlan(plan) }),
  updateColregsConstraint: (constraint) => set({ colregsConstraint: normalizeColregsConstraint(constraint) }),
  updateSafetyAlert: (alert) => set({ safetyAlert: normalizeSafetyAlert(alert) }),
  updateThreatState: (threat) => set((s) => {
    const normalized = normalizeThreatState(threat);
    if (!normalized) {
      return { threatState: null };
    }
    const primary = normalized.targets.find((target) => target.primary) ?? normalized.targets[0];
    const nextHistory = primary ? [
      ...s.threatRiskHistory,
      {
        t: normalized.stampS ?? Date.now() / 1000,
        primaryTargetId: primary.targetId,
        riskPhase: primary.riskPhase,
        riskScore: primary.riskScore,
        warningMarginM: primary.warningMarginM,
        dangerMarginM: primary.dangerMarginM,
        dcpaM: primary.dcpaM,
        tcpaS: primary.tcpaS,
      },
    ].slice(-MAX_THREAT_HISTORY) : s.threatRiskHistory;
    return { threatState: normalized, threatRiskHistory: nextHistory };
  }),
  reset: () => set(initialState),
}));

if (import.meta.env.DEV) {
  (window as any).__TELEMETRY_STORE__ = useTelemetryStore;
}
